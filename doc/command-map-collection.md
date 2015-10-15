MAP 명령
--------

Map collection에 관한 명령은 아래와 같다.

- [Map collection 생성: mop create](command-map-collection.md#mop-create---map-collection-생성)
- Map collection 삭제: delete (기존 key-value item의 삭제 명령을 그대로 사용)

Map element에 관한 명령은 아래와 같다. 

- [Map element 삽입: mop insert](command-map-collection.md#mop-insert---map-element-삽입)
- [Map element 삭제: mop delete](command-map-collection.md#mop-delete---map-element-삭제)
- [Map element 조회: mop get](command-map-collection.md#mop-get---map-element-조회)
- [Map element 다중조회: mop mget](command-map-collection.md#mop-mget---map-element-다중-조회)

### mop create - Map Collection 생성

Map collection을 empty 상태로 생성한다.

```
mop create <key> <attributes> [noreply]\r\n
* <attributes>: <flags> <exptime> <maxcount> [<ovflaction>] [unreadable]
```

- \<key\> - 대상 item의 key string
- \<attributes\> - 설정할 item attributes. [Item Attribute 설명](/doc/arcus-item-attribute.md)을 참조 바란다.
- noreply - 명시하면, response string을 전달받지 않는다.

Response string과 그 의미는 아래와 같다.

- "CREATED" - 성공
- "EXISTS" - 동일 key string을 가진 item이 이미 존재
- “CLIENT_ERROR bad command line format” - protocol syntax 틀림
- “SERVER_ERROR out of memory” - 메모리 부족

### mop insert - Map Element 삽입

Map collection에 하나의 element를 삽입한다.
Map collection을 생성하면서 하나의 element를 삽입할 수도 있다.

```
mop insert <key> <bytes> [create <attributes>] [noreply|pipe]\r\n<data>\r\n
* <attributes>: <flags> <exptime> <maxcount> [<ovflaction>] [unreadable]
```

- \<key\> - 대상 item의 key string
- \<bytes\> - 삽입할 데이터 길이 (trailing 문자인 "\r\n"을 제외한 길이)
- create \<attributes\> - map collection 없을 시에 map 생성 요청.
                    [Item Attribute 설명](/doc/arcus-item-attribute.md)을 참조 바란다.
- noreply or pipe - 명시하면, response string을 전달받지 않는다. 
                    pipe 사용은 [Command Pipelining](/doc/command-pipelining.md)을 참조 바란다.
- \<data\> - 삽입할 데이터 (최대 4KB)

Response string과 그 의미는 아래와 같다.

- "STROED" - 성공 (element만 삽입)
- “CREATED_STORED” - 성공 (collection 생성하고 element 삽입)
- “NOT_FOUND” - key miss
- “TYPE_MISMATCH” - 해당 item이 map colleciton이 아님
- “OVERFLOWED” - overflow 발생
- “CLIENT_ERROR bad command line format” - protocol syntax 틀림
- “CLIENT_ERROR too large value” - 삽입할 데이터가 4KB 보다 큼
- “CLIENT_ERROR bad data chunk” - 삽입할 데이터 길이가 \<bytes\>와 다르거나 "\r\n"으로 끝나지 않음
- “SERVER_ERROR out of memory” - 메모리 부족

### mop delete - Map Element 삭제

Map collection에서 하나의 element를 삭제한다.

```
mop delete <key> <bytes> [drop] [noreply|pipe]\r\n<data>\r\n
```

- \<key\> - 대상 item의 key string
- \<bytes\> - 삭제할 데이터 길이 (trailing 문자인 "\r\n"을 제외한 길이)
- drop - element 삭제로 인해 empty map이 될 경우, 그 map을 drop할 것인지를 지정한다.
- noreply or pipe - 명시하면, response string을 전달받지 않는다. 
                    pipe 사용은 [Command Pipelining](/doc/command-pipelining.md)을 참조 바란다.
- \<data\> - 삭제할 데이터 (최대 4KB)

Response string과 그 의미는 아래와 같다.

- "DELETED" - 성공 (element만 삭제 - 중복 element가 있을 경우 하나만 삭제)
- “DELETED_DROPPED” - 성공 (element 삭제하고 collection을 drop한 상태)
- “NOT_FOUND” - key miss
- “NOT_FOUND_ELEMENT” - element miss (삭제할 element가 없음)
- “TYPE_MISMATCH” - 해당 item이 map colleciton이 아님
- “CLIENT_ERROR bad command line format” - protocol syntax 틀림
- “CLIENT_ERROR too large value” - 삭제할 데이터가 4KB 보다 큼
- “CLIENT_ERROR bad data chunk” - 삭제할 데이터의 길이가 \<bytes\>와 다르거나 “\r\n”으로 끝나지 않음

### mop get - Map Element 조회

Map collection에서 N 개의 elements를 조회한다.

```
mop get <key> <count> [delete|drop]\r\n
```

- \<key\> - 대상 item의 key string
- \<count\> - 조회할 elements 개수를 지정. 0이면 전체 elements를 의미한다.
- delete or drop - element 조회하면서 그 element를 delete할 것인지
                   그리고 delete로 인해 empty map이 될 경우 그 map을 drop할 것인지를 지정한다.

성공 시의 response string은 아래와 같다.
VALUE 라인의 \<count\>는 조회된 element 개수를 의미한다. 
마지막 라인은 END, DELETED, DELETED_DROPPED 중의 하나를 가지며
각각 element 조회만 수행한 상태, element 조회하고 삭제한 상태,
element 조회 및 삭제하고 map을 drop한 상태를 의미한다.

```
VALUE <flags> <count>\r\n
<bytes> <data>\r\n
<bytes> <data>\r\n
<bytes> <data>\r\n
...
END|DELETED|DELETED_DROPPED\r\n
```

실패 시의 response string과 그 의미는 아래와 같다.

- “NOT_FOUND”	- key miss
- “NOT_FOUND_ELEMENT”	- element miss (element가 존재하지 않는 상태임)
- “TYPE_MISMATCH”	- 해당 item이 map collection이 아님
- “UNREADABLE” - 해당 item이 unreadable item임
- “CLIENT_ERROR bad command line format” - protocol syntax 틀림
- "SERVER_ERROR out of memory [writing get response]”	- 메모리 부족

### mop mget - Map Element 다중 조회

Map collection에서 N 개의 key와 각 key에 대해 M 개의 elements를 조회한다.

```
mop mget key_count <key_1> <count_1> ㆍㆍㆍ <key_n> <count_n> [delete|drop]\r\n
```

- key_count - 한번에 조회 할 key 개수
- \<key\> - 대상 item의 key string
- \<count\> - 조회할 elements 개수를 지정. 0이면 전체 elements를 의미한다.
- delete or drop - element 조회하면서 그 element를 delete할 것인지
                   그리고 delete로 인해 empty map이 될 경우 그 map을 drop할 것인지를 지정한다.

성공 시의 response string은 아래와 같다.
key_count에 따라서 N개의 response string이 출력된다.
VALUE 라인의 \<count\>는 조회된 element 개수를 의미한다. 
마지막 라인은 END, DELETED, DELETED_DROPPED 중의 하나를 가지며
각각 element 조회만 수행한 상태, element 조회하고 삭제한 상태,
element 조회 및 삭제하고 map을 drop한 상태를 의미한다.

```
VALUE <flags> <count>\r\n
<bytes> <data>\r\n
<bytes> <data>\r\n
<bytes> <data>\r\n
...
END|DELETED|DELETED_DROPPED\r\n
VALUE <flags> <count>\r\n
<bytes> <data>\r\n
<bytes> <data>\r\n
<bytes> <data>\r\n
...
END|DELETED|DELETED_DROPPED\r\n
```

실패 시의 response string과 그 의미는 아래와 같다.

- “NOT_FOUND”	- key miss
- “NOT_FOUND_ELEMENT”	- element miss (element가 존재하지 않는 상태임)
- “TYPE_MISMATCH”	- 해당 item이 map collection이 아님
- “UNREADABLE” - 해당 item이 unreadable item임
- “CLIENT_ERROR bad command line format” - protocol syntax 틀림
- "SERVER_ERROR out of memory [writing get response]”	- 메모리 부족
 

