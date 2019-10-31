Arcus cache server는 하나의 데이터만을 value로 가지는 simple key-value 외에도
여러 데이터를 구조화된 형태로 저장하는 collection을 하나의 value로 가지는
확장된 key-value 데이터 모델을 제공한다.

## 1-1. Basic Terms

Arcus cache server의 key-value 모델은 아래의 기본 제약 사항을 가진다.

- 기존 key-value 모델의 제약 사항
  - Key의 최대 크기는 32000 character이다. (arcus-memcached 1.11 이후 버전)
    - 기존 버전에서 key 최대 크기는 250 character이다.
  - Value의 최대 크기는 1MB(trailing 문자인 “\r\n” 포함한 길이) 이다.
- Collection 제약 사항
  - 하나의 collection에 들어갈 수 있는 최대 element 개수는 50,000개이다.
  - Collection의 각 element가 가지는 value의 최대 크기는 4KB(trailing 문자인 “\r\n” 포함한 길이) 이다.

### Cache Key

Cache key는 Arcus cache server에 저장할 데이터를 대표하는 코드이다. Cache key 형식은 아래와 같다.

```
  Cache Key : [<prefix>:]<subkey>
```

- \<prefix\> - Cache key의 앞에 붙는 namespace이다.
  - Prefix 단위로 cache server에 저장된 key들을 그룹화하여 flush하거나 통계 정보를 볼 수 있다.
  - Prefix를 생략할 수도 있지만, 가급적 사용하길 권한다.
- delimiter - Prefix와 subkey를 구분하는 문자로 default delimiter는 콜론(‘:’)이다.
- \<subkey\> - 일반적으로 응용에서 사용하는 Key이다.

Prefix와 subkey는 명명 규칙을 가지므로 주의하여야 한다.
Prefix는 영문 대소문자, 숫자, 언더바(_), 하이픈(-), 플러스(+), 점(.) 문자만으로 구성될 수 있으며,
이 중에 하이픈(-)은 prefix 명의 첫번째 문자로 올 수 없는 제약이 있다.
Subkey는 공백을 포함할 수 없으며, 기본적으로 alphanumeric만을 사용하길 권장한다.

### Cache Item

Arcus cache server는 simple key-value 외에 collection 지원으로 다양한 item 유형을 가진다.

- simple key-value item - 기존 key-value item
- collection item
  - list item - 데이터들의 linked list을 value가지는 item
  - set item - 유일한 데이터들의 집합을 value로 가지는 item
  - map item - \<field, value\>쌍으로 구성된 데이터 집합을 value로 가지는 item 
  - b+tree item - b+tree key 기반으로 정렬된 데이터 집합을 value로 가지는 item

### Expiration, Eviction, and Sticky

각 cache item은 expiration time 속성을 가지며,
이 값의 설정을 통해 expire되지 않는 item 또는 특정 시간 이후에 자동 expire될 item을 지정할 수 있다.
이에 대한 자세한 설명은 [Item Attribute 설명](./Chapter-1.-ARCUS-Basic-Concept.md#1-3-item-attributes)을 참고 바란다.

Arcus cache server는 memory cache이며, 한정된 메모리 공간을 사용하여 데이터를 caching한다.
메모리 공간이 모두 사용된 상태에서 새로운 item 저장 요청이 들어올 경우,
Arcus cache server는 "out of memory" 오류를 내거나 LRU 기반의 eviction 방식
즉, 가장 오랫동안 접근되지 않은 item을 제거하고 새로운 item 저장을 허용하는 방식을 사용한다.
이러한 동작 방식은 Arcus cache server의 -M 구동 옵션을 지정 가능하며,
default로는 LRU 기반의 eviction 방식을 사용한다.

특정 응용에서는 어떤 item이 expire & evict 대상이 되지 않기를 원하는 경우도 있다.
Arcus cache server는 이러한 item을 sticky item이라 하며, 
expiration time을 -1로 지정하면, sticky item으로 지원한다.
Sticky item의 삭제는 전적으로 응용에 의해 관리되어야 함을 주의해야 한다.

Sticky items은 일반적으로 많지 않을 것으로 예상하지만,
응용의 실수로 인해 sticky item들이 Arcus 서버의 전체 메모리 공간을 차지하게 되는 경우를 방지하기 위하여,
전체 메모리 공간의 일부만이 sticky items에 의해 사용되도록 설정하는 -g(gummed or sticky) 구동 옵션을 제공한다.
Sticky items의 메모리 공간으로 사용될 메모리 비율이며, 0 ~ 100 범위의 값으로 지정가능하다.
디폴트인 0은 sticky items을 허용하지 않는다는 것이며,
100은 전체 메모리를 sticky items 저장 용도로 사용할 수 있음을 의미한다.

### Memory Allocator

Arcus cache server는 item 메모리 공간의 할당과 반환을 효율적으로 관리할 목적으로
두 가지 memory allocator를 사용한다.

**Slab Allocator**

Slab allocator는 메모리 크기 별로 메모리 공간을 나누어 관리하기 위해 slab class로 구분하고,
각 slab class에서 동일 크기의 메모리 공간들인 slab들을 free list 형태로 관리하면서
그 크기의 메모리 공간의 할당과 반환을 신속히 처리해 주는 memory allocator이다.
기존 memcached에서 사용되던 대표적인 memory allocator이다.

최대 slab 크기는 현재 1MB이다. 최소 slab 크기 즉, 첫 번째 slab class의 slab 크기와
그 다음 slab class들의 slab 크기는 아래의 Arcus cache server 구동 옵션으로 설정한다.

- \-n \<bytes\> : minimum space allocated from key+value+flags (default: 48)
  - 최소 크기의 slab 크기를 결정한다.
- \-f \<factor\> : chunk size growth factor (default: 1.25)
  - Slab class 별로 slab 크기의 증가 정도를 지정하며, 1.0보다 큰 값으로 지정해야 한다.
  

**Small Memory Allocator**

Collection 지원으로 인해 작은 메모리 공간의 할당과 반환 요청이 많아졌다.
이러한 작은 메모리 공간을 효율적으로 관리하기 위하여
small memory allocator를 새로 개발하여 사용하고 있다.
8000 바이트 이하의 메모리 공간은 small memory allocator가 담당하며,
8000 바이트 초과의 메모리 공간은 기존 slab allocator가 담당한다.

### Slab Class 별 LRU 리스트

Arcus cache server는 slab class 별 LRU 리스트를 유지하고,
eviction 대상 item으로 오랫동안 접근되지 않은 item이 선택될 수 있게 한다.

Small memory allocator 추가로 인해, slab class 별 LRU 리스트에 변동 사항이 있다.
특별히, 0번 slab class를 두어 small memory allocator가 사용하고 있으며,
small memory allocator로 부터 메모리 공간을 할당받는
작은 크기의 key-value items과 collection items은 0번 LRU 리스트에 연결된다.
따라서, 8000 바이트 이하의 메모리 공간에 해당하는
기존 slab class의 LRU 리스트들은 empty가 상태가 된다.

## 1-2. Collection Concept

Collection 유형과 그 구조 및 특징은 아래와 같다.

**List** - linked list

> Element들의 doubly linked list 구조를 가진다.
>   Head element와 tail element 정보를 유지하면서, head/tail에서 시작하여 forward/backward 방향으로
>   특정 위치에 있는 element를 접근할 수 있다.
>   많은 element를 가진 list에서 중간 위치의 임의 element 접근 시에 성능 이슈가 있으므로,
>   list를 queue 개념으로 사용하길 권한다.

**Set** - unordered set of unique value

> Set 자료 구조는 membership checking에 적합하다.
>   Unordered set of unique value 저장을 위해 내부적으로 hash table 구조를 사용한다.
>   하나의 set에 들어가는 elements 개수에 비례하여 hash table 전체 크기를 동적으로 조정하기 위해,
>   일반적인 tree 구조와 유사하게 여러 depth로 구성되는 hash table 구조를 가진다.

**Map** - unordered set of \<field, value\>

> Map 자료 구조는 \<field, value\> 쌍을 저장한다.
>   Field 값의 유일성 보장과 field 기준으로 해당 element 탐색을 빠르게 하기 위한 hash table 구조를 사용한다.
>   하나의 map에 들어가는 elements 개수에 비례하여 hash table 전체 크기를 동적으로 조정하기 위해,
>   일반적인 tree 구조와 유사하게 여러 depth로 구성되는 hash table 구조를 가진다.


**B+tree** - sorted map based on b+tree key

> 각 element 마다 unique key를 두고, 이를 기준으로 정렬된 elements 집합을 b+tree 구조로 저장하며,
>   이러한 unique key 기반으로 forward/backward 방향의 range scan을 제공한다.
>   Elements 수에 비례하여 동적으로 depth를 조정하는 b+tree 구조를 사용하여 메모리 사용을 최소화한다.
>   그 외에, b+tree의 nonleaf node는 각 하위 node 중심의 sub-tree에 저장된 element 개수 정보를
>   담고 있도록 해서, 특정 element의 position 조회 및 position 기반의 element 조회 기능도 제공한다.

Collection item은 \<key, "collection meta info"\> 구조를 가진다.
Collection meta info는 collection 유형에 따른 속성 정보를 가지며,
해당 collection의 elements에 신속히 접근하기 정보를 가진다.
예를 들어, list의 head/tail element 주소, set의 최상위 hash table 주소,
map의 최상위 hash table 구조, b+tree의 root node 주소가 이에 해당된다.

### Element 구조

Collection 유형에 따른 element 구조는 아래와 같다.

- list/set element : \< data \>

  각 element는 하나의 데이터 만을 가진다.

- map element : \<field(map element key), data \>

  map에서 각 element를 구분하기 위한 field를 필수적으로 가지며,
  field는 중복을 허용하지 않는다.

- b+tree element : \< bkey(b+tree key), eflag(element flag), data \>

  b+tree에서 elements를 어떤 기준으로 정렬하기 위한 bkey를 필수적으로 가지며,
  옵션 사항으로 bkey 기반의 scan 시에 특정 element를 filtering하기 위한 eflag를 가질 수 있으며,
  bkey에 종속되어 단순 저장/조회 용도로 사용되는 data를 가진다.


### BKey (B+Tree Key)

B+tree collection에서 사용가능한 bkey 데이터 유형은 아래 두 가지이다.

- 8 bytes unsigned integer

  0 ~ 18446744073709551615 범위의 값을 지정할 수 있다.
  이 유형이 성능 및 메모리 공간 관점에서 hexadecimal 유형보다 유리하므로, 이 유형의 bkey 사용을 권장한다.

- hexadecimal

  “0x”로 시작하는 짝수 개의 hexadecimal 문자열로 표현하며, 대소문자 모두 사용 가능하다.
  Arcus cache server는 두 hexadecimal 문자를 1 byte로 저장하며,
  1 ~ 31 길이의 variable length byte array로 저장한다.

  hexadecimal 표현이 올바른 경우의 저장 바이트 수와 잘못된 경우의 이유는 아래와 같다.

  | hexadecimal value | storage bytes | incorrect reason              |
  | ----------------- | ------------- | ----------------------------- |
  | 0x34F40056        | 4 bytes       |                               |
  | 0xabcd00778899    | 6 bytes       |                               |
  | 34F40056          |               | 앞에 "0x"가 없음              |
  | 0x34F40           |               | 홀수 개의 hexadecimal 문자열  |
  | 0x34F40G          |               | 'G'가 hexadecimal 문자가 아님 |

bkey의 대소 비교는 8 bytes unsigned integer 유형의 값이면 두 integer 값의 단순한 비교 연산으로 수행하며, 
hexadecimal 유형의 값이면 아래와 같은 lexicographical order로 두 값을 비교한다.

- 두 hexadecimal의 첫째 바이트부터 차례로 바이트 단위로 대소를 비교하여, 차이나면 대소 비교를 종료한다.
- 두 hexadecimal 중 작은 길이만큼의 비교에서 두 값이 동일하면, 긴 길이의 hexadecimal 값이 크다고 판단한다.
- 두 hexadecimal의 길이도 같고 각 바이트의 값도 동일하면, 두 hexadecimal 값은 같다라고 판단한다.

### EFlag (Element Flag)

eflag는 현재 b+tree element에만 존재하는 필드이다.
eflag 데이터 유형은 hexadecimal 유형만 가능하며,
bkey의 hexadecimal 표현과 저장 방식을 그대로 따른다. 

### EFlag Filter

eflag에 대한 filter 조건은 아래와 같이 표현하며,
(1) eflag의 전체/부분 값과 특정 값과의 compare 연산이나
(2) eflag의 전체/부분 값에 대해 어떤 operand로 bitwise 연산을 취한 후의 결과와 특정 값과의 compare 연산이다.

```
eflag_filter: <fwhere> [<bitwop> <foperand>] <compop> <fvalue>
```

- \<fwhere\> 
  - eflag 값에서 bitwise/compare 연산을 취할 시작 offset을 바이트 단위로 나타낸다.
    bitwise/compare 연산을 취할 데이터의 length는 \<fvalue\>의 length로 한다.
    예를 들어, eflag 전체 데이터를 선택한다면, \<fwhere\>는 0이어야 하고
    \<fvalue\>의 length는 eflag 전체 데이터의 length와 동일하여야 한다.
- [\<bitwop\> \<foperand\>]
  - 생략 가능하며, eflag에 대한 bitwise 연산을 지정한다.
  - bitwise 연산이 지정되면 이 연산의 결과가 compare 연산의 대상이 되며,
    생략된다면 eflag 값 자체가 compare 연산의 대상이 된다.
  - \<bitwop\>는 “&”(bitwise and), “|”(bitwise or), “^”(bitwise xor) 중의 하나로 bitwise 연산을 지정한다.
  - \<foperand\>는 bitwise 연산을 취할 operand로 hexadecimal로 표현한다.
    \<foperand\>의 길이는 compare 연산을 취한 \<fvalue\>의 길이와 동일하여야 한다.
- \<compop\> \<fvalue\>  
  - eflag에 대한 compare 연산을 지정한다.
  - \<compop\>는 "EQ", "NE', "LT", "LE", "GT", "GE" 중의 하나로 compare 연산을 지정하며,
    \<fvalue\>는 compare 연산을 취할 대상 값으로 마찬가지로 hexadecimal로 표현한다.
  - IN 또는 NOT IN 조건을 명시할 수도 있다. 
    IN 조건은 "EQ" 연산과 comma separated hexadecimal values로 명시하면 되고,
    NOT IN 조건은 "NE" 연산과 comma separated hexadecimal values로 명시하면 된다.
    이 경우, comma로 구분된 hexadecimal values의 최대 수는 100 개까지만 지원한다.

하나의 b+tree의 element에는 동일 길이의 element flag를 사용하길 권장한다.
하지만 응용이 필요하다면, 하나의 b+tree에 소속된 elements 이더라도
eflag가 생략될 수도 있고 서로 다른 길이의 eflag를 가질 수도 있다.
이 경우, 아래와 같이 eflag filtering이 애매모호해 질 수 있다.
이 상황에서는 filter 조건의 비교 연산이 “NE”이면 true로 판별하고, 그 외의 비교 연산이면 false로 판별한다.

- eflag가 없는 element에 eflag_filter 조건이 주어질 수 있다.
- eflag가 있지만 eflag_filter 조건에서 명시된 offset과 length의 데이터를 가지지 않을 수 있다.
  예를 들어, eflag가 4 bytes인 상황에서
  (1) eflag_filter 조건의 offset은 5인 경우이거나 
  (2) eflag_filter 조건의 offset은 3이고 length는 4인 경우가 있을 수 있다.

### EFlag Update

Eflag의 전체 또는 부분 값에 update 연산도 가능하며 아래와 같이 표현한다.
Eflag 전체 변경은 새로운 eflag 값으로 교체하는 것이며,
부분 변경은 eflag의 부분 데이터에 대해 bitwise 연산을 취한 결과로 교체한다.

```
eflag_update: [<fwhere> <bitwop>] <fvalue>
```

- [\<fwhere\> \<bitwop\>]
  - eflag를 부분 변경할 경우만 지정한다.
  - \<fwhere>은 eflag에서 부분 변경할 데이터의 시작 offset을 바이트 단위로 나타내며,
    이 경우, 부분 변경할 데이터의 length는 뒤에 명시되는 \<fvalue\>의 length로 결정된다.
  - \<bitwop\>는 부분 변경할 데이터에 대한 취할 bitwise 연산으로,
    “&”(bitwise and), “|”(bitwise or), “^”(bitwise xor) 중의 하나로 지정할 수 있다.
- \<fvalue\>
  - 변경할 new value를 나타낸다.
  - 앞서 기술한 \<fwhere\>과 \<bitwop\>가 생략되면, eflag의 전체 데이터를 \<fvalue\>로 변경한다.
    부분 변경을 위한 \<fwhere\>과 \<bitwop\>가 지정되면
    \<fvalue\>는 eflag 부분 데이터에 대해 bitwise 연산을 취할 operand로 사용되며,
    bitwise 연산의 결과가 eflag의 new value로 변경된다.

기존 eflag 값을 delete하여 eflag가 없는 상태로 변경할 수 있다.
이를 위해서는 \<fwhere\>과 \<bitwop\>를 생략하고 \<fvalue\> 값으로 0을 주면 된다.

## 1-3. Item Attributes

Arcus cache server는 collection 기능 지원으로 인해,
기존 key-value item 유형 외에 list, set, map, b+tree item 유형을 가진다.
각 item 유형에 따라 설정/조회 가능한 속성들(attributes)이 구분되며, 이들의 개요는 아래 표와 같다.
아래 표는 각 속성이 적용되는 item 유형, 속성의 간단한 설명, 허용가능한 값들과 디폴트 값을 나타낸다.

```
|-----------------------------------------------------------------------------------------------------------------|
| Attribute Name | Item Type   | Description           | Allowed Values                 | Default Value           |
|-----------------------------------------------------------------------------------------------------------------|
| flags          | all         | data specific flags   | 4 bytes unsigned integer       | 0                       |
|-----------------------------------------------------------------------------------------------------------------|
| expiretime     | all         | item expiration time  | 4 bytes singed integer         | 0                       |
|                |             |                       |  -1: sticky                    |                         |
|                |             |                       |   0: never expired             |                         |
|                |             |                       |  >0: expired in the future     |                         |
|-----------------------------------------------------------------------------------------------------------------|
| type           | all         | item type             | "kv", "list", "set", "map",    | N/A                     |
|                |             |                       | "b+tree"                       |                         |
|-----------------------------------------------------------------------------------------------------------------|
| count          | collection  | current # of elements | 4 bytes unsigned integer       | N/A                     |
|-----------------------------------------------------------------------------------------------------------------|
| maxcount       | collection  | maximum # of elements | 4 bytes unsigned integer       | 4000                    |
|-----------------------------------------------------------------------------------------------------------------|
| overflowaction | collection  | overflow action       | “error”: all collections       | list: "tail_trim"       |
|                |             |                       | “head_trim”: list              | set: "error"            |
|                |             |                       | “tail_trim”: list              | map: "error"            |
|                |             |                       | “smallest_trim”: b+tree        | b+tree: "smallest_trim" |
|                |             |                       | “largest_trim”: b+tree         |                         |
|                |             |                       | “smallest_silent_trim”: b+tree |                         |
|                |             |                       | “largest_silent_trim”: b+tree  |                         |
|-----------------------------------------------------------------------------------------------------------------|
| readable       | collection  | readable/unreable     | “on”, “off”                    | "on"                    |
|-----------------------------------------------------------------------------------------------------------------|
| maxbkeyrange   | b+tree only | maximum bkey range    | 8 bytes unsigned integer or    | 0                       |
|                |             |                       | hexadecimal (max 31 bytes)     |                         |
|-----------------------------------------------------------------------------------------------------------------|
```

Arcus cache server는 item 속성들을 조회하거나 변경하는 용도의 getattr 명령과 setattr 명령을 제공한다.
이들 명령에 대한 자세한 설명은 [Item Attribute 명령](./Chapter-8.-Item-Attribute-Command.md )을 참고 바란다.


Item 속성들 중 정확한 이해를 돕기 위해 추가 설명이 필요한 속성들에 대해 아래에서 자세히 설명한다.

### flags 속성

Flags는 item의 data-specific 정보를 저장하기 위한 목적으로 사용된다.
예를 들어, Arcus java client는 어떤 java object를 cache server에 저장할 경우,
그 java object의 type에 따라 serialization(or marshalling)하여 저장할 data를 만들고, 
그 java object의 type 정보를 flags 값으로 하여 Arcus cache server에 요청하여 저장한다.
Data 조회 시에는 Arcus cache server로 부터 data와 함께 flags 정보를 함께 얻어와서,
해당 java object의 type에 따라 그 data를 de-serialization(or de-marshalling)하여 java object를 생성한다.

### expiretime 속성

Item의 expiretime 속성으로 그 item의 expiration time을 초(second) 단위로 설정한다.

Arcus cache server는 expire 되지 않고 메모리 부족 상황에서도 evict 되지 않는 sticky item 기능을 제공한다.
Sticky item 또한 expiretime 속성으로 지정한다.

- -1 : sticky item으로 설정
- 0	: never expired item으로 설정, 그러나 메모리 부족 시에 evict될 수 있다.
- X <= (60 * 60 * 24 * 30) : 30일 이하의 값이면, 실제 expiration time은 "현재 시간 + X(초)"로 결정된다.
      -2 이하이면, 그 즉시 expire 된다.
- X > (60 * 60 * 24 * 30) : 30일 초과의 값이면, 실제 expiration time은 "X"로 결정된다.
      이 경우, X를 unix time으로 인식하여 expiration time으로 설정하는 것이며,
      X가 현재 시간보다 작으면 그 즉시 expire 된다.

### maxcount 속성

Collection item에만 유효한 속성으로, 하나의 collection에 저장할 수 있는 최대 element 수를 규정한다.

Maxcount 속성의 hard limit과 default(설정 생략 또는 0을 값으로 주는 경우) 값은 아래와 같다.

- hard limit : 50000
- default value : 4000

Maxcount 속성의 hard limit을 작게 규정한 이유는 O(small N)의 수행 비용을 가지도록 하기 위한 것이다.
Event-driven processing 모델에 따라
하나의 worker thread가 비동기 방식으로 여러 client requests를 처리해야 하는 상황에서,
한 request의 처리 비용이 가급적 작아야만 다른 request의 execution latency에 주는 영향을 최소화할 수 있다.

### overflowaction 속성

Collection의 maxcount를 초과하여 element 추가하면 overflow가 발생하며, 이 경우 취할 action을 지정한다.

- "error"
  - 모든 collection 유형에 설정 가능한 속성이다.
  - set과 map collection의 default overflow action이다.
  - 새로운 element 추가를 허용하지 않고 overflow 오류를 리턴한다. 
- "head_trim", "tail_trim"
  - list collection에만 설정 가능한 overflow action이다.
  - list collection의 default overflow action은 "tail_trim"이다.
  - 새로운 element 추가를 허용하는 대신 list의 head 또는 tail에 위치한 기존 element를 제거한다.
  - Overflow trim 발생 시, trim 발생 여부를 나타내는 trim flag를 내부적으로 유지하지 않는다.
- "smallest_trim", "largest_trim"
  - b+tree collecton에만 설정 가능한 overflow action이다.
  - b+tree collecton의 default overflow action은 "smallest_trim"이다.
  - 새로운 element 추가를 허용하는 대신 smallest bkey 또는largest bkey를 가진 기존 element를 제거한다.
  - Overflow trim 발생 시, trim 발생 여부를 나타내는 trim flag를 내부적으로 유지하며,
    trim 발생한 bkey 영역을 조회할 경우 응답 결과에 trim 발생 여부를 포함시킨다.
- "smallest_silent_trim", "largest_silent_trim"
  - "samllest_trim", "largest_trim"과 동일하게 동작하는 overflow action이다.
  - 차이점은 overflow trim이 발생하더라도 trim flag를 내부적으로 유지하지 않으며,
    trim 발생한 bkey 영역을 조회하더라도 조회 결과에 trim 발생 여부를 포함시키지 않는다.
  - 응용에서 주의할 사항은 trim 여부나 trim된 데이터에 대한 검사를 직접 수행하여야 한다.

참고로, 아래에 기술하는 maxbkeyrange 속성에 따라 element를 제거하는 경우에도
overflow action이 참조된다.

### readable 속성

Arcus cache server는 다수 element를 가진 collection을 atomic하게 생성하는 명령을 제공하지 않는다.
대신, 하나의 element를 추가하는 명령을 반복 수행함으로써 원하는 collection을 만들 수 있다.
이 경우, 하나의 collection이 완성되기 전의 incomplete collection이 응용에게 노출될 수 있는 문제가 있다.
예를 들어, 어떤 사용자의 SNS 친구 정보를 set collection 형태로 cache에 저장한다고 가정한다.
일부 친구 정보만 set collection에 저장된 상태에서 그 사용자의 전체 친구 정보를 조회하는 요청이 들어온다면,
incomplete 친구 정보가 응용에게 노출되게 된다.
이러한 문제를 방지하기 위해 collection 생성에 대해 read atomicity를 제공하는 기능이 필요하며,
이 기능의 구현을 위해 readable 속성을 제공한다.

처음 empty collection 생성 시에 readable 속성을 off 상태로 설정해서
그 collection에 대한 조회 요청은 UNREADABLE 오류를 발생시키게 하고,
그 collection에 모든 element들을 추가한 후에 마지막으로 readable 속성을 다시 on 상태로 변경함으로써
complete collection이 응용에 의해 조회될 수 있게 할 수 있다.

### maxbkeyrange 속성

B+tree only 속성으로 smallest bkey와 largest bkey의 최대 범위를 규정한다.
B+tree에 설정된 maxbkeyrange를 위배시키는 새로운 bkey를 가진 element를 삽입하는 경우,
b+tree의 overflow action 정책에 따라 오류를 내거나
smallest/largest bkey를 가진 elements를 제거함으로써 항상 maxbkeyrange 특성을 준수하게 한다.

Maxbkeyrange 속성에 의한 element 제거는 응용 요청에 의한 명시적인 element 제거와 동일하므로,
trim으로 처리하지 않는다. 결국, maxcount 속성에 의한 overflow trim 만을 trim으로 처리한다.

maxbkeyrange의 사용 예로,
어떤 응용이 data 생성 시간을 bkey로 하여 그 data를 b+tree에 저장하고
최근 2일치 data 만을 b+tree에 유지하길 원한다고 가정한다.
초 단위의 시간 값을 bkey 값으로 사용한다면,
maxbkeyrange는 2일치에 해당하는 값인 172880(2 * 24 * 60 * 60)으로 지정하고,
최근 data만을 보관하기 위해 overflowaction은 "smallest_trim"으로 지정하면 된다.
이러한 지정으로, 새로운 data가 추가될 때마다 b+tree에서 2일치가 지난 data는
maxbkeyrange와 overflowaction에 의해 자동으로 제거된다.
만약, 이런 기능이 없다면, 응용에서 오래된(2일이 지난) data를 직접 제거하는 작업을 수행해야 한다.

maxbkeyrange 설정은 bkey의 데이터 유형에 맞게 설정하여야 하며,
maxbkeyrange 설정이 생략되거나 명시적으로 0을 줄 경우의 default 값은
bkey 데이터 유형에 무관하게 unlimited maxbkeyrange를 의미한다.