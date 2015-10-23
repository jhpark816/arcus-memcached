/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * arcus-memcached - Arcus memory cache server
 * Copyright 2010-2014 NAVER Corp.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "config.h"
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

#include "default_engine.h"

#define hashsize(n) ((uint32_t)1<<(n))
#define hashmask(n) (hashsize(n)-1)

#ifdef MWJIN_HASHTABLE_EXPANSION_BY_WORKERS
#define GET_HASH_BUCKET(hash, mask)        ((hash) & (mask))
#define GET_HASH_TABIDX(hash, shift, mask) (((hash) >> (shift)) & (mask))
#endif

#define DEFAULT_PREFIX_HASHPOWER 10
#define DEFAULT_PREFIX_MAX_DEPTH 1

typedef struct {
    prefix_t   *pt;
    uint8_t     nprefix;
    uint32_t    hash;
} prefix_t_list_elem;

static EXTENSION_LOGGER_DESCRIPTOR *logger;

static prefix_t *root_pt = NULL;

static inline void *_get_prefix(prefix_t *prefix)
{
    return (void*)(prefix + 1);
}

ENGINE_ERROR_CODE assoc_init(struct default_engine *engine)
{
    logger = engine->server.log->get_logger();
#ifdef MWJIN_HASHTABLE_EXPANSION_BY_WORKERS
    engine->assoc.hashsize = hashsize(engine->assoc.hashpower);
    engine->assoc.hashmask = hashmask(engine->assoc.hashpower);
    engine->assoc.rootpower = 0;

    engine->assoc.roottable = calloc(engine->assoc.hashsize * 2, sizeof(void *));
    if (engine->assoc.roottable == NULL) {
        return ENGINE_ENOMEM;
    } else {
        engine->assoc.roottable[0].hashtable = (hash_item**)&engine->assoc.roottable[engine->assoc.hashsize];
    }

    engine->assoc.powertable = calloc(engine->assoc.hashsize, sizeof(uint32_t*));
    if (engine->assoc.powertable == NULL) {
        free(engine->assoc.roottable);
        return ENGINE_ENOMEM;
    }

    engine->assoc.prefix_hashtable = calloc(hashsize(DEFAULT_PREFIX_HASHPOWER), sizeof(void *));
    if (engine->assoc.prefix_hashtable == NULL) {
        free(engine->assoc.roottable);
        free(engine->assoc.powertable);
        return ENGINE_ENOMEM;
    }
#else
    engine->assoc.primary_hashtable = calloc(hashsize(engine->assoc.hashpower), sizeof(void *));
    if (engine->assoc.primary_hashtable == NULL) {
        return ENGINE_ENOMEM;
    }
    engine->assoc.prefix_hashtable = calloc(hashsize(DEFAULT_PREFIX_HASHPOWER), sizeof(void *));
    if (engine->assoc.prefix_hashtable == NULL) {
        free(engine->assoc.primary_hashtable);
        engine->assoc.primary_hashtable = NULL;
        return ENGINE_ENOMEM;
    }
#endif

    // initialize noprefix stats info
    memset(&engine->assoc.noprefix_stats, 0, sizeof(prefix_t));
    root_pt = &engine->assoc.noprefix_stats;
    return ENGINE_SUCCESS;
}

void assoc_final(struct default_engine *engine)
{
#ifdef MWJIN_HASHTABLE_EXPANSION_BY_WORKERS
    int ii, table_count;

    for (ii=0; ii < engine->assoc.rootpower; ++ii) {
         table_count = hashsize(ii); //2 ^ n
         free(engine->assoc.roottable[table_count].hashtable);
    }
    free(engine->assoc.roottable);
    free(engine->assoc.powertable);
    free(engine->assoc.prefix_hashtable);
#else
    int sleep_count = 0;
    while (engine->assoc.expanding && engine->assoc.threadrun) {
        usleep(1000); // 1ms
        sleep_count++;
    }
    if (sleep_count > 100) { /* waited too long */
        logger->log(EXTENSION_LOG_INFO, NULL,
                "Waited %d ms for hash table expantion to be stopped.\n", sleep_count);
    }

    free(engine->assoc.primary_hashtable);
    free(engine->assoc.prefix_hashtable);
#endif
}

#ifdef MWJIN_HASHTABLE_EXPANSION_BY_WORKERS
static void redistribute(struct default_engine *engine, unsigned int bucket)
{
    hash_item *it, **prev;
    uint32_t tabidx;
    uint32_t ii, table_count = hashsize(engine->assoc.powertable[bucket]);

    for (ii=0; ii < table_count; ++ii) {
         prev = &engine->assoc.roottable[ii].hashtable[bucket];
         while (*prev != NULL) {
             it = *prev;
             tabidx = GET_HASH_TABIDX(engine->server.core->hash(item_get_key(it), it->nkey, 0),
                                      engine->assoc.hashpower, hashmask(engine->assoc.rootpower));
             if (tabidx == ii) {
                 prev = &it->h_next;
             } else {
                 *prev = it->h_next;
                 it->h_next = engine->assoc.roottable[tabidx].hashtable[bucket];
                 engine->assoc.roottable[tabidx].hashtable[bucket] = it;
             }
         }
    }

    engine->assoc.powertable[bucket] = engine->assoc.rootpower;
}
#endif

hash_item *assoc_find(struct default_engine *engine, uint32_t hash, const char *key, const size_t nkey)
{
    hash_item *it;
#ifdef MWJIN_HASHTABLE_EXPANSION_BY_WORKERS
    uint32_t bucket = GET_HASH_BUCKET(hash, engine->assoc.hashmask);
    uint32_t tabidx = GET_HASH_TABIDX(hash, engine->assoc.hashpower, hashmask(engine->assoc.powertable[bucket]));

    it = engine->assoc.roottable[tabidx].hashtable[bucket];
#else
    unsigned int oldbucket;

    if (engine->assoc.expanding &&
        (oldbucket = (hash & hashmask(engine->assoc.hashpower - 1))) >= engine->assoc.expand_bucket)
    {
        it = engine->assoc.old_hashtable[oldbucket];
    } else {
        it = engine->assoc.primary_hashtable[hash & hashmask(engine->assoc.hashpower)];
    }
#endif
    hash_item *ret = NULL;
    int depth = 0;
    while (it) {
        if ((nkey == it->nkey) && (memcmp(key, item_get_key(it), nkey) == 0)) {
            ret = it;
            break;
        }
        it = it->h_next;
        ++depth;
    }

    MEMCACHED_ASSOC_FIND(key, nkey, depth);
    return ret;
}

/* returns the address of the item pointer before the key.  if *item == 0,
   the item wasn't found */
static hash_item** _hashitem_before(struct default_engine *engine,
                                    uint32_t hash, const char *key, const size_t nkey)
{
    hash_item **pos;
#ifdef MWJIN_HASHTABLE_EXPANSION_BY_WORKERS
    uint32_t bucket = GET_HASH_BUCKET(hash, engine->assoc.hashmask);
    uint32_t tabidx = GET_HASH_TABIDX(hash, engine->assoc.hashpower, hashmask(engine->assoc.powertable[bucket]));

    pos = &engine->assoc.roottable[tabidx].hashtable[bucket];
#else
    unsigned int oldbucket;

    if (engine->assoc.expanding &&
        (oldbucket = (hash & hashmask(engine->assoc.hashpower - 1))) >= engine->assoc.expand_bucket)
    {
        pos = &engine->assoc.old_hashtable[oldbucket];
    } else {
        pos = &engine->assoc.primary_hashtable[hash & hashmask(engine->assoc.hashpower)];
    }
#endif
    while (*pos && ((nkey != (*pos)->nkey) || memcmp(key, item_get_key(*pos), nkey))) {
        pos = &(*pos)->h_next;
    }
    return pos;
}

#define DEFAULT_HASH_BULK_MOVE 10

#ifdef MWJIN_HASHTABLE_EXPANSION_BY_WORKERS
#else
static void *assoc_maintenance_thread(void *arg)
{
    struct default_engine *engine = arg;
    struct timespec sleep_time = {0, 1000};
    hash_item *it, *next;
    int  bucket;
    int  hash_bulk_move = DEFAULT_HASH_BULK_MOVE;
    int  i,try_cnt = 9;
    long tot_execs = 0;
    bool done = false;

    engine->assoc.threadrun = true;

    if (engine->config.verbose) {
        logger->log(EXTENSION_LOG_INFO, NULL, "Hash table expansion start: %d => %d\n",
                    hashsize(engine->assoc.hashpower-1), hashsize(engine->assoc.hashpower));
    }

    while (engine->initialized)
    {
        /* Long-running background task. */
        /* Hold the cache lock lazily in order to give priority to normal workers. */
        for (i = 0; i < try_cnt; i++) {
            if (pthread_mutex_trylock(&engine->cache_lock) == 0) break;
            nanosleep(&sleep_time, NULL); /* 1 us */
        }
        if (i >= try_cnt) pthread_mutex_lock(&engine->cache_lock);

        /* Expand hash table incrementally */
        for (i = 0; i < hash_bulk_move && engine->assoc.expanding; i++) {
            it = engine->assoc.old_hashtable[engine->assoc.expand_bucket];
            for ( ;  it != NULL; it = next) {
                next = it->h_next;
                bucket = engine->server.core->hash(item_get_key(it), it->nkey, 0)
                       & hashmask(engine->assoc.hashpower);
                it->h_next = engine->assoc.primary_hashtable[bucket];
                engine->assoc.primary_hashtable[bucket] = it;
            }
            engine->assoc.old_hashtable[engine->assoc.expand_bucket] = NULL;
            engine->assoc.expand_bucket++;
            if (engine->assoc.expand_bucket == hashsize(engine->assoc.hashpower-1)) {
                engine->assoc.expanding = false;
                free(engine->assoc.old_hashtable);
                engine->assoc.old_hashtable = NULL;
                done = true;
            }
        }
        /* Release the cache lock */
        pthread_mutex_unlock(&engine->cache_lock);
        if (done) break;

        if ((++tot_execs % 100) == 0) {
            nanosleep(&sleep_time, NULL); /* 1 us */
        }
    }

    if (done) {
        if (engine->config.verbose) {
            logger->log(EXTENSION_LOG_INFO, NULL, "Hash table expansion done\n");
        }
    } else {
        logger->log(EXTENSION_LOG_INFO, NULL, "Hash table expansion stopped.\n");
    }

    engine->assoc.threadrun = false;
    return NULL;
}
#endif

/* grows the hashtable to the next power of 2. */
static void assoc_expand(struct default_engine *engine)
{
#ifdef MWJIN_HASHTABLE_EXPANSION_BY_WORKERS
    uint32_t ii, table_count = hashsize(engine->assoc.rootpower); // 2 ^ n
    hash_item** new_hashtable;

    new_hashtable = calloc(engine->assoc.hashsize * table_count, sizeof(void *));
    if (new_hashtable) {
        for (ii=0; ii < table_count; ++ii) {
            engine->assoc.roottable[table_count+ii].hashtable = &new_hashtable[engine->assoc.hashsize*ii];
        }
        engine->assoc.rootpower++;
    }
#else
    engine->assoc.old_hashtable = engine->assoc.primary_hashtable;

    engine->assoc.primary_hashtable = calloc(hashsize(engine->assoc.hashpower + 1), sizeof(void *));
    if (engine->assoc.primary_hashtable) {
        engine->assoc.hashpower++;
        engine->assoc.expanding = true;
        engine->assoc.expand_bucket = 0;

        /* start a thread to do the expansion */
        int ret = 0;
        pthread_t tid;
        pthread_attr_t attr;

        if (pthread_attr_init(&attr) != 0 ||
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0 ||
            (ret = pthread_create(&tid, &attr, assoc_maintenance_thread, engine)) != 0)
        {
            fprintf(stderr, "Can't create thread: %s\n", strerror(ret));
            engine->assoc.hashpower--;
            engine->assoc.expanding = false;
            free(engine->assoc.primary_hashtable);
            engine->assoc.primary_hashtable = engine->assoc.old_hashtable;
            engine->assoc.old_hashtable = NULL;
        }

    } else {
        engine->assoc.primary_hashtable = engine->assoc.old_hashtable;
        engine->assoc.old_hashtable = NULL;
        /* Bad news, but we can keep running. */
    }
#endif
}

/* Note: this isn't an assoc_update.  The key must not already exist to call this */
int assoc_insert(struct default_engine *engine, uint32_t hash, hash_item *it)
{
#ifdef MWJIN_HASHTABLE_EXPANSION_BY_WORKERS
    uint32_t bucket = GET_HASH_BUCKET(hash, engine->assoc.hashmask);
    uint32_t tabidx;

    if (engine->assoc.powertable[bucket] != engine->assoc.rootpower) {
        redistribute(engine, bucket);
    }
    tabidx = GET_HASH_TABIDX(hash, engine->assoc.hashpower, hashmask(engine->assoc.powertable[bucket]));

    assert(assoc_find(engine, hash, item_get_key(it), it->nkey) == 0);  /* shouldn't have duplicately named things defined */

    // inserting actual hash_item to appropriate assoc_t
    it->h_next = engine->assoc.roottable[tabidx].hashtable[bucket];
    engine->assoc.roottable[tabidx].hashtable[bucket] = it;

    engine->assoc.hash_items++;
    if (engine->assoc.hash_items > (hashsize(engine->assoc.hashpower + engine->assoc.rootpower) * 3) / 2) {
        assoc_expand(engine);
    }
#else
    unsigned int oldbucket;

    assert(assoc_find(engine, hash, item_get_key(it), it->nkey) == 0);  /* shouldn't have duplicately named things defined */

    // inserting actual hash_item to appropriate assoc_t
    if (engine->assoc.expanding &&
            (oldbucket = (hash & hashmask(engine->assoc.hashpower - 1))) >= engine->assoc.expand_bucket)
    {
        it->h_next = engine->assoc.old_hashtable[oldbucket];
        engine->assoc.old_hashtable[oldbucket] = it;
    } else {
        it->h_next = engine->assoc.primary_hashtable[hash & hashmask(engine->assoc.hashpower)];
        engine->assoc.primary_hashtable[hash & hashmask(engine->assoc.hashpower)] = it;
    }

    engine->assoc.hash_items++;
    if (! engine->assoc.expanding && engine->assoc.hash_items > (hashsize(engine->assoc.hashpower) * 3) / 2) {
        assoc_expand(engine);
    }
#endif
    MEMCACHED_ASSOC_INSERT(item_get_key(it), it->nkey, engine->assoc.hash_items);
    return 1;
}

void assoc_delete(struct default_engine *engine, uint32_t hash, const char *key, const size_t nkey)
{
    hash_item **before = _hashitem_before(engine, hash, key, nkey);

    if (*before) {
        hash_item *nxt;
        engine->assoc.hash_items--;

       /* The DTrace probe cannot be triggered as the last instruction
         * due to possible tail-optimization by the compiler
         */
        MEMCACHED_ASSOC_DELETE(key, nkey, engine->assoc.hash_items);
        nxt = (*before)->h_next;
        (*before)->h_next = 0;   /* probably pointless, but whatever. */
        *before = nxt;

        return;
    }
    /* Note:  we never actually get here.  the callers don't delete things
       they can't find. */
    assert(*before != 0);
}

/*
 * Prefix Management
 */
prefix_t *assoc_prefix_find(struct default_engine *engine, uint32_t hash, const char *prefix, const size_t nprefix)
{
    prefix_t *pt;

    pt = engine->assoc.prefix_hashtable[hash & hashmask(DEFAULT_PREFIX_HASHPOWER)];
    while (pt) {
        if ((nprefix == pt->nprefix) && (memcmp(prefix, _get_prefix(pt), nprefix) == 0)) {
            return pt;
        }
        pt = pt->h_next;
    }
    return NULL;
}

static prefix_t** _prefixitem_before(struct default_engine *engine,
                                     uint32_t hash, const char *prefix, const size_t nprefix)
{
    prefix_t **pos;

    pos = &engine->assoc.prefix_hashtable[hash & hashmask(DEFAULT_PREFIX_HASHPOWER)];
    while (*pos && ((nprefix != (*pos)->nprefix) || memcmp(prefix, _get_prefix(*pos), nprefix))) {
        pos = &(*pos)->h_next;
    }
    return pos;
}

static int _prefix_insert(struct default_engine *engine, uint32_t hash, prefix_t *pt)
{
    assert(assoc_prefix_find(engine, hash, _get_prefix(pt), pt->nprefix) == NULL);

    pt->h_next = engine->assoc.prefix_hashtable[hash & hashmask(DEFAULT_PREFIX_HASHPOWER)];
    engine->assoc.prefix_hashtable[hash & hashmask(DEFAULT_PREFIX_HASHPOWER)] = pt;

    assert(pt->parent_prefix != NULL);
    pt->parent_prefix->prefix_items++;
    engine->assoc.tot_prefix_items++;
    return 1;
}

static void _prefix_delete(struct default_engine *engine, uint32_t prefix_hash, const char *prefix, const uint8_t nprefix)
{
    prefix_t **prefix_before = _prefixitem_before(engine, prefix_hash, prefix, nprefix);
    prefix_t *pt = *prefix_before;
    prefix_t *prefix_nxt = NULL;

    assert(pt != NULL && pt->parent_prefix != NULL);

    pt->parent_prefix->prefix_items--;
    engine->assoc.tot_prefix_items--;

    prefix_nxt = pt->h_next;
    pt->h_next = 0;
    *prefix_before = prefix_nxt;

    // release
    free(pt);
}

bool assoc_prefix_isvalid(struct default_engine *engine, hash_item *it)
{
    rel_time_t current_time = engine->server.core->get_current_time();
    prefix_t *pt;

    if (it->nprefix == it->nkey) {
        /* the prefix of key: null */
        assert(root_pt != NULL);
        pt = root_pt;
        if (pt->oldest_live != 0 && pt->oldest_live <= current_time && it->time <= pt->oldest_live) {
            return false;
        }
    } else {
        /* the prifix of key: given */
        pt = assoc_prefix_find(engine, engine->server.core->hash(item_get_key(it), it->nprefix, 0),
                               item_get_key(it), it->nprefix);
        while (pt != NULL && pt != root_pt) {
            // validation check between prefix and hash_item
            if (pt->oldest_live != 0 && pt->oldest_live <= current_time && it->time <= pt->oldest_live) {
                return false;
            }
            // traversal parent prefixes to validate
            pt = pt->parent_prefix;
        }
    }
    return true;
}

void assoc_prefix_update_size(prefix_t *pt, ENGINE_ITEM_TYPE item_type, const size_t item_size, const bool increment)
{
    assert(pt != NULL);

    // update prefix information
    if (increment == true) {
        if (item_type == ITEM_TYPE_KV)         pt->hash_items_bytes += item_size;
        else if (item_type == ITEM_TYPE_LIST)  pt->list_hash_items_bytes += item_size;
        else if (item_type == ITEM_TYPE_SET)   pt->set_hash_items_bytes += item_size;
#ifdef MAP_COLLECTION_SUPPORT
        else if (item_type == ITEM_TYPE_MAP)   pt->map_hash_items_bytes += item_size;
#endif
        else if (item_type == ITEM_TYPE_BTREE) pt->btree_hash_items_bytes += item_size;
#if 0 // might be used later
        if (1) {
            prefix_t *curr_pt = pt->parent_prefix;
            while (curr_pt != NULL) {
                curr_pt->tot_hash_items_bytes += item_size;
                curr_pt = curr_pt->parent_prefix;
            }
        }
#endif
    } else {
        if (item_type == ITEM_TYPE_KV)         pt->hash_items_bytes -= item_size;
        else if (item_type == ITEM_TYPE_LIST)  pt->list_hash_items_bytes -= item_size;
        else if (item_type == ITEM_TYPE_SET)   pt->set_hash_items_bytes -= item_size;
#ifdef MAP_COLLECTION_SUPPORT
        else if (item_type == ITEM_TYPE_MAP)   pt->map_hash_items_bytes -= item_size;
#endif
        else if (item_type == ITEM_TYPE_BTREE) pt->btree_hash_items_bytes -= item_size;
#if 0 // might be used later
        if (1) {
            prefix_t *curr_pt = pt->parent_prefix;
            while (curr_pt != NULL) {
                curr_pt->tot_hash_items_bytes -= item_size;
                curr_pt = curr_pt->parent_prefix;
            }
        }
#endif
    }
}

ENGINE_ERROR_CODE assoc_prefix_link(struct default_engine *engine,
                                    hash_item *it, const size_t item_size, prefix_t **pfx_item)
{
    assert(it->nprefix == 0);
    const char *key = item_get_key(it);
    size_t     nkey = it->nkey;
    int prefix_depth = 0;
    int i = 0;
    char *token;
    prefix_t *pt = NULL;
    prefix_t_list_elem prefix_list[DEFAULT_PREFIX_MAX_DEPTH];

    // prefix discovering: we don't even know prefix existence at this time
    while ((token = memchr(key + i + 1, engine->config.prefix_delimiter, nkey - i - 1)) != NULL) {
        i = token - key;
        prefix_list[prefix_depth].nprefix = i;

        prefix_depth++;
        if (prefix_depth >= DEFAULT_PREFIX_MAX_DEPTH) {
            break;
        }
    }

    if (prefix_depth == 0) {
        pt = root_pt;
        time(&pt->create_time);
        it->nprefix = nkey;
    } else {
        for (i = prefix_depth - 1; i >= 0; i--) {
            prefix_list[i].hash = engine->server.core->hash(key, prefix_list[i].nprefix, 0);
            pt = assoc_prefix_find(engine, prefix_list[i].hash, key, prefix_list[i].nprefix);
            if (pt != NULL) break;
        }

        if (i < (prefix_depth - 1)) {
            if (prefix_depth == 1) {
                if (!mc_isvalidname(key, prefix_list[0].nprefix)) {
                    return ENGINE_PREFIX_ENAME; /* Invalid prefix name */
                }
            }

            // need building prefixes
            if (pt != NULL && i >= 0) {
                prefix_list[i].pt = pt; // i >= 0
            }

            for (int j = i + 1; j < prefix_depth; j++) {
                pt = (prefix_t*)malloc(sizeof(prefix_t) + prefix_list[j].nprefix + 1);
                if (pt == NULL) {
                    for (j = j - 1; j >= i + 1; j--) {
                        assert(prefix_list[j].pt != NULL);
                        _prefix_delete(engine, prefix_list[j].hash, key, prefix_list[j].nprefix);
                    }
                    return ENGINE_ENOMEM;
                }

                // building a prefix_t
                memset(pt, 0, sizeof(prefix_t));
                memcpy(pt + 1, key, prefix_list[j].nprefix);
                memcpy((char*)pt+sizeof(prefix_t)+prefix_list[j].nprefix, "\0", 1);
                pt->nprefix = prefix_list[j].nprefix;
                pt->parent_prefix = (j == 0 ? root_pt : prefix_list[j-1].pt);
                time(&pt->create_time);

                // registering allocated prefixes to prefix hastable
                _prefix_insert(engine, prefix_list[j].hash, pt);
                prefix_list[j].pt = pt;
            }
        }
        // update item information about prefix length
        it->nprefix = pt->nprefix;
    }

    assert(pt != NULL);

    // update prefix information
    if ((it->iflag & ITEM_IFLAG_LIST) != 0) {
        pt->list_hash_items++;
        pt->list_hash_items_bytes += item_size;
    } else if ((it->iflag & ITEM_IFLAG_SET) != 0) {
        pt->set_hash_items++;
        pt->set_hash_items_bytes += item_size;
#ifdef MAP_COLLECTION_SUPPORT
    } else if ((it->iflag & ITEM_IFLAG_MAP) != 0) {
        pt->map_hash_items++;
        pt->map_hash_items_bytes += item_size;
#endif
    } else if ((it->iflag & ITEM_IFLAG_BTREE) != 0) {
        pt->btree_hash_items++;
        pt->btree_hash_items_bytes += item_size;
    } else {
        pt->hash_items++;
        pt->hash_items_bytes += item_size;
    }
#if 0 // might be used later
    if (1) {
        curr_pt = pt->parent_prefix;
        while (curr_pt != NULL) {
            curr_pt->tot_hash_items++;
            curr_pt->tot_hash_items_bytes += item_size;
            curr_pt = curr_pt->parent_prefix;
        }
    }
#endif

    *pfx_item = pt;
    return ENGINE_SUCCESS;
}

void assoc_prefix_unlink(struct default_engine *engine, hash_item *it, const size_t item_size)
{
    prefix_t *pt;
    assert(it->nprefix != 0);

    if (it->nprefix == it->nkey) {
        pt = root_pt;
    } else {
        pt = assoc_prefix_find(engine, engine->server.core->hash(item_get_key(it), it->nprefix, 0),
                               item_get_key(it), it->nprefix);
    }
    assert(pt != NULL);

    // update prefix information
    if ((it->iflag & ITEM_IFLAG_LIST) != 0) {
        pt->list_hash_items--;
        pt->list_hash_items_bytes -= item_size;
    } else if ((it->iflag & ITEM_IFLAG_SET) != 0) {
        pt->set_hash_items--;
        pt->set_hash_items_bytes -= item_size;
#ifdef MAP_COLLECTION_SUPPORT
    } else if ((it->iflag & ITEM_IFLAG_MAP) != 0) {
        pt->map_hash_items--;
        pt->map_hash_items_bytes -= item_size;
#endif
    } else if ((it->iflag & ITEM_IFLAG_BTREE) != 0) {
        pt->btree_hash_items--;
        pt->btree_hash_items_bytes -= item_size;
    } else {
        pt->hash_items--;
        pt->hash_items_bytes -= item_size;
    }
#if 0 // might be used later
    if (1) {
        prefix_t *curr_pt = pt->parent_prefix;
        while (curr_pt != NULL) {
            curr_pt->tot_hash_items--;
            curr_pt->tot_hash_items_bytes -= item_size;
            curr_pt = curr_pt->parent_prefix;
        }
    }
#endif

    while (pt != NULL) {
        prefix_t *parent_pt = pt->parent_prefix;
#ifdef MAP_COLLECTION_SUPPORT
        if (pt != root_pt && pt->prefix_items == 0 && pt->hash_items == 0 && pt->list_hash_items == 0 &&
            pt->set_hash_items == 0 && pt->map_hash_items == 0 && pt->btree_hash_items == 0) {
            assert(pt->hash_items_bytes == 0 && pt->list_hash_items_bytes == 0 &&
                   pt->set_hash_items_bytes == 0 && pt->map_hash_items_bytes == 0 &&
                   pt->btree_hash_items_bytes == 0);
            _prefix_delete(engine, engine->server.core->hash(_get_prefix(pt), pt->nprefix, 0),
                           _get_prefix(pt), pt->nprefix);
#else
        if (pt != root_pt && pt->prefix_items == 0 && pt->hash_items == 0 &&
            pt->list_hash_items == 0 && pt->set_hash_items == 0 && pt->btree_hash_items == 0) {
            assert(pt->hash_items_bytes == 0 && pt->list_hash_items_bytes == 0 &&
                   pt->set_hash_items_bytes == 0 && pt->btree_hash_items_bytes == 0);
            _prefix_delete(engine, engine->server.core->hash(_get_prefix(pt), pt->nprefix, 0),
                           _get_prefix(pt), pt->nprefix);
#endif
        } else {
            break;
        }
        pt = parent_pt;
    }
}

#if 0 // might be used later
static uint32_t do_assoc_count_invalid_prefix(struct default_engine *engine)
{
    prefix_t *pt;
    uint32_t i, size = hashsize(DEFAULT_PREFIX_HASHPOWER);
    uint32_t invalid_prefix = 0;

    for (i = 0; i < size; i++) {
        pt = engine->assoc.prefix_hashtable[i];
        while (pt) {
            if (pt->prefix_items == 0 && pt->hash_items == 0 &&
                pt->list_hash_items == 0 && pt->set_hash_items == 0 && pt->btree_hash_items == 0) {
                invalid_prefix++;
            }
            pt = pt->h_next;
        }
    }
    return invalid_prefix;
}
#endif

static ENGINE_ERROR_CODE do_assoc_get_prefix_stats(struct default_engine *engine,
                                                   const char *prefix, const int  nprefix, void *prefix_data)
{
    prefix_t *pt;

    if (nprefix < 0) { // all prefix information
        char *buf;
        struct tm *t;
#ifdef MAP_COLLECTION_SUPPORT
        const char *format = "PREFIX %s itm %llu kitm %llu litm %llu sitm %llu mitm %llu bitm %llu "
                             "tsz %llu ktsz %llu ltsz %llu stsz %llu mtsz %llu btsz %llu time %04d%02d%02d%02d%02d%02d\r\n";
#else
        const char *format = "PREFIX %s itm %llu kitm %llu litm %llu sitm %llu bitm %llu "
                             "tsz %llu ktsz %llu ltsz %llu stsz %llu btsz %llu time %04d%02d%02d%02d%02d%02d\r\n";
#endif
        uint32_t i, hsize = hashsize(DEFAULT_PREFIX_HASHPOWER);
        uint32_t num_prefixes = engine->assoc.tot_prefix_items;
        uint32_t tot_prefix_name_len = 0;
        uint32_t msize, pos, written;

        pt = root_pt;
#ifdef MAP_COLLECTION_SUPPORT
        if (pt != NULL && (pt->hash_items > 0 || pt->list_hash_items > 0 || pt->set_hash_items > 0 || pt->map_hash_items > 0 || pt->btree_hash_items > 0)) {
#else
        if (pt != NULL && (pt->hash_items > 0 || pt->list_hash_items > 0 || pt->set_hash_items > 0 || pt->btree_hash_items > 0)) {
#endif
            /* including null prefix */
            num_prefixes += 1;
            tot_prefix_name_len = strlen("<null>");
        }
        for (i = 0; i < hsize; i++) {
            pt = engine->assoc.prefix_hashtable[i];
            while (pt) {
                tot_prefix_name_len += pt->nprefix;
                pt = pt->h_next;
            }
        }

        msize = sizeof(uint32_t) + strlen(format) + tot_prefix_name_len
                + num_prefixes * (strlen(format) - 2 /* %s */
                                  + (10 * (20 - 4))) /* %llu replaced by 20-digit num */
                - (5 * (4 - 2)) /* %02d replaced by 2-digit num */
                + sizeof("END\r\n");

        buf = malloc(msize);
        if (buf == NULL) {
            return ENGINE_ENOMEM;
        }
        pos = sizeof(uint32_t);

        pt = root_pt;
#ifdef MAP_COLLECTION_SUPPORT
        if (pt != NULL && (pt->hash_items > 0 || pt->list_hash_items > 0 || pt->set_hash_items > 0 || pt->map_hash_items > 0 || pt->btree_hash_items > 0)) {

            /* including null prefix */
            t = localtime(&pt->create_time);
            written = snprintf(buf+pos, msize-pos, format, "<null>",
                               pt->hash_items+pt->list_hash_items+pt->set_hash_items+pt->map_hash_items+pt->btree_hash_items,
                               pt->hash_items,pt->list_hash_items,pt->set_hash_items,pt->map_hash_items,pt->btree_hash_items,
                               pt->hash_items_bytes+pt->list_hash_items_bytes+pt->set_hash_items_bytes+pt->map_hash_items_bytes+pt->btree_hash_items_bytes,
                               pt->hash_items_bytes,pt->list_hash_items_bytes,pt->set_hash_items_bytes,pt->map_hash_items_bytes,pt->btree_hash_items_bytes,
                               t->tm_year+1900, t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
            pos += written;
        }

        for (i = 0; i < hsize; i++) {
            pt = engine->assoc.prefix_hashtable[i];
            while (pt) {
                t = localtime(&pt->create_time);
                written = snprintf(buf+pos, msize-pos, format, _get_prefix(pt),
                               pt->hash_items+pt->list_hash_items+pt->set_hash_items+pt->map_hash_items+pt->btree_hash_items,
                               pt->hash_items,pt->list_hash_items,pt->set_hash_items,pt->map_hash_items,pt->btree_hash_items,
                               pt->hash_items_bytes+pt->list_hash_items_bytes+pt->set_hash_items_bytes+pt->map_hash_items_bytes+pt->btree_hash_items_bytes,
                               pt->hash_items_bytes,pt->list_hash_items_bytes,pt->set_hash_items_bytes,pt->map_hash_items_bytes,pt->btree_hash_items_bytes,
                               t->tm_year+1900, t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
                pos += written;
                assert(pos < msize);
                pt = pt->h_next;
            }
        }
#else
        if (pt != NULL && (pt->hash_items > 0 || pt->list_hash_items > 0 || pt->set_hash_items > 0 || pt->btree_hash_items > 0)) {

            /* including null prefix */
            t = localtime(&pt->create_time);
            written = snprintf(buf+pos, msize-pos, format, "<null>",
                               pt->hash_items+pt->list_hash_items+pt->set_hash_items+pt->btree_hash_items,
                               pt->hash_items,pt->list_hash_items,pt->set_hash_items,pt->btree_hash_items,
                               pt->hash_items_bytes+pt->list_hash_items_bytes+pt->set_hash_items_bytes+pt->btree_hash_items_bytes,
                               pt->hash_items_bytes,pt->list_hash_items_bytes,pt->set_hash_items_bytes,pt->btree_hash_items_bytes,
                               t->tm_year+1900, t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
            pos += written;
        }

        for (i = 0; i < hsize; i++) {
            pt = engine->assoc.prefix_hashtable[i];
            while (pt) {
                t = localtime(&pt->create_time);
                written = snprintf(buf+pos, msize-pos, format, _get_prefix(pt),
                               pt->hash_items+pt->list_hash_items+pt->set_hash_items+pt->btree_hash_items,
                               pt->hash_items,pt->list_hash_items,pt->set_hash_items,pt->btree_hash_items,
                               pt->hash_items_bytes+pt->list_hash_items_bytes+pt->set_hash_items_bytes+pt->btree_hash_items_bytes,
                               pt->hash_items_bytes,pt->list_hash_items_bytes,pt->set_hash_items_bytes,pt->btree_hash_items_bytes,
                               t->tm_year+1900, t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
                pos += written;
                assert(pos < msize);
                pt = pt->h_next;
            }
        }
#endif
        memcpy(buf+pos, "END\r\n", 6);
        *(uint32_t*)buf = pos + 5 - sizeof(uint32_t);

        *(char**)prefix_data = buf;
    } else {
        prefix_engine_stats *prefix_stats = (prefix_engine_stats*)prefix_data;

        if (prefix != NULL) {
            pt = assoc_prefix_find(engine, engine->server.core->hash(prefix,nprefix,0), prefix, nprefix);
        } else {
            pt = root_pt;
        }
        if (pt == NULL) {
            return ENGINE_PREFIX_ENOENT;
        }

        prefix_stats->hash_items = pt->hash_items;
        prefix_stats->hash_items_bytes = pt->hash_items_bytes;
        prefix_stats->prefix_items = pt->prefix_items;
        if (prefix != NULL)
            prefix_stats->tot_prefix_items = pt->prefix_items;
        else
            prefix_stats->tot_prefix_items = engine->assoc.tot_prefix_items;
    }
    return ENGINE_SUCCESS;
}

ENGINE_ERROR_CODE assoc_get_prefix_stats(struct default_engine *engine,
                                         const char *prefix, const int nprefix, void *prefix_data)
{
    ENGINE_ERROR_CODE ret;
    pthread_mutex_lock(&engine->cache_lock);
    ret = do_assoc_get_prefix_stats(engine, prefix, nprefix, prefix_data);
    pthread_mutex_unlock(&engine->cache_lock);
    return ret;
}
