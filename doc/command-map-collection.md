MAP 명령
--------

Map collection에 관한 명령은 아래와 같다.

- [Map collection 생성: mop create](command-map-collection.md#mop-create---map-collection-생성)
- Map collection 삭제: delete (기존 key-value item의 삭제 명령을 그대로 사용)

Map element에 관한 명령은 아래와 같다. 

- [Map element 삽입: mop insert](command-map-collection.md#mop-insert---map-field-element-삽입)
- [Map element 변경: mop update](command-map-collection.md#mop-update---map-element-변경)
- [Map element 삭제: mop delete](command-map-collection.md#mop-delete---map-field-element-삭제)
- [Map element 조회: mop get](command-map-collection.md#mop-get---map-field-element-조회)
- [하나의 명령으로 여러 b+tree들에 대한 조회를 한번에 수행하는 기능:  mop mget](command-map-collection.md#mop-mget---set-element-존재유무-검사)

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

### mop insert - Map Field, Element 삽입

Map collection에 하나의 element를 삽입한다.
Map collection을 생성하면서 하나의 element를 삽입할 수도 있다.

```
mop insert <key> <field> <bytes> [create <attributes>] [noreply|pipe]\r\n<data>\r\n
* <attributes>: <flags> <exptime> <maxcount> [<ovflaction>] [unreadable]
```

- \<key\> - 대상 item의 key string
- \<field\> - 대상 item의 field string
- \<bytes\> - 삽입할 데이터 길이 (trailing 문자인 "\r\n"을 제외한 길이)
- create \<attributes\> - set collection 없을 시에 set 생성 요청.
                    [Item Attribute 설명](/doc/arcus-item-attribute.md)을 참조 바란다.
- noreply or pipe - 명시하면, response string을 전달받지 않는다. 
                    pipe 사용은 [Command Pipelining](/doc/command-pipelining.md)을 참조 바란다.
- \<data\> - 삽입할 데이터 (최대 4KB)

Response string과 그 의미는 아래와 같다.

- "STROED" - 성공 (field, element 삽입)
- “CREATED_STORED” - 성공 (collection 생성하고 field, element 삽입)
- “NOT_FOUND” - key miss
- “TYPE_MISMATCH” - 해당 item이 map colleciton이 아님
- “OVERFLOWED” - overflow 발생
- "FIELD_EXISTS" - 동일 데이터를 가진 field가 존재. map field uniqueness 위배
- “CLIENT_ERROR bad command line format” - protocol syntax 틀림
- “CLIENT_ERROR too large value” - 삽입할 데이터가 4KB 보다 큼
- “CLIENT_ERROR bad data chunk” - 삽입할 데이터 길이가 \<bytes\>와 다르거나 "\r\n"으로 끝나지 않음
- “SERVER_ERROR out of memory” - 메모리 부족

### mop update - Map Element 변경

Map collection에서 하나의 field에 대해 element 변경을 수행한다.
현재 다수 field에 대한 변경연산은 제공하지 않고 있다.

```
mop update <key> <field> <bytes> [noreply|pipe]\r\n
```

- \<key\> - 대상 item의 key string
- \<field\> - 대상 item의 field string
- \<bytes\> - 변경할 데이터 길이 (trailing 문자인 "\r\n"을 제외한 길이)
- noreply or pipe - 명시하면, response string을 전달받지 않는다. 
                    pipe 사용은 [Command Pipelining](/doc/command-pipelining.md)을 참조 바란다.
- \<data\> - 변경할 데이터 (최대 4KB)

Response string과 그 의미는 아래와 같다.

- "UPDATED" - 성공 (element 변경)
- “NOT_FOUND” - key miss
- "NOT_FOUND_FIELD" - field miss
- “TYPE_MISMATCH” - 해당 item이 map colleciton이 아님
- “OVERFLOWED” - overflow 발생
- “CLIENT_ERROR bad command line format” - protocol syntax 틀림
- “CLIENT_ERROR too large value” - 삽입할 데이터가 4KB 보다 큼
- “CLIENT_ERROR bad data chunk” - 삽입할 데이터 길이가 \<bytes\>와 다르거나 "\r\n"으로 끝나지 않음
- “SERVER_ERROR out of memory” - 메모리 부족

### mop delete - Map Field, Element 삭제

Map collection에서 하나의 field 또는 지정한 field 조건을 만족하는 N개의 element를 삭제한다.

```
mop delete <key> <field_count> [<"space separated fields">] [drop] [noreply|pipe]\r\n
```

- \<key\> - 대상 item의 key string
- \<field_count\> - 삭제할 field 개수를 지정, 0이면 전체 field, element를 의미한다.
- "space separated fields" - 대상 map의 field list로, 띄어쓰기(" ")로 구분한다. field_count가 0이면 생략 가능하다.
- drop - field, element 삭제로 인해 empty map이 될 경우, 그 map을 drop할 것인지를 지정한다.
- noreply or pipe - 명시하면, response string을 전달받지 않는다. 
                    pipe 사용은 [Command Pipelining](/doc/command-pipelining.md)을 참조 바란다.

Response string과 그 의미는 아래와 같다.

- "DELETED" - 성공 (field, element 삭제)
- “DELETED_DROPPED” - 성공 (field, element 삭제하고 collection을 drop한 상태)
- “NOT_FOUND” - key miss
- “NOT_FOUND_FIELD” - field miss (삭제할 field, element가 없음)
- “TYPE_MISMATCH” - 해당 item이 map colleciton이 아님
- “CLIENT_ERROR bad command line format” - protocol syntax 틀림

### mop get - Map Field, Element 조회

Map collection에서 하나의 field 또는 지정한 field 조건을 만족하는 N개의 element를 조회한다.

```
mop delete <key> <field_count> [<"space separated fields">] [delete|drop]\r\n
```

- \<key\> - 대상 item의 key string
- \<field_count\> - 조회할 field 개수를 지정, 0이면 전체 field, element를 의미한다.
- "space separated fields" - 대상 map의 field list로, 띄어쓰기(" ")로 구분한다. field_count가 0이면 생략 가능하다.
- delete or drop - field, element 조회하면서 그 field, element를 delete할 것인지
                   그리고 delete로 인해 empty map이 될 경우 그 map을 drop할 것인지를 지정한다.

성공 시의 response string은 아래와 같다.
VALUE 라인의 \<count\>는 조회된 field 개수를 의미한다. 
마지막 라인은 END, DELETED, DELETED_DROPPED 중의 하나를 가지며
각각 field 조회만 수행한 상태, field 조회하고 삭제한 상태,
field 조회 및 삭제하고 map을 drop한 상태를 의미한다.

```
VALUE <flags> <count>\r\n
<field> <bytes> <data>\r\n
<field> <bytes> <data>\r\n
<field> <bytes> <data>\r\n
...
END|DELETED|DELETED_DROPPED\r\n
```

실패 시의 response string과 그 의미는 아래와 같다.

- “NOT_FOUND”	- key miss
- “NOT_FOUND_FIELD”	- field miss (field, element가 존재하지 않는 상태임)
- “TYPE_MISMATCH”	- 해당 item이 map collection이 아님
- “UNREADABLE” - 해당 item이 unreadable item임
- “CLIENT_ERROR bad command line format” - protocol syntax 틀림
- "SERVER_ERROR out of memory [writing get response]”	- 메모리 부족

### mop mget - Set Element 존재유무 검사

여러 map들에 대해 동일 조회 조건(field)으로 element들을 한꺼번에 조회한다.

```
mop mget <lenkeys> <numkeys> <field_count> <"space separated fields">\r\n
<”comma separated keys”>\r\n
```

- \<”comma separated keys”\> - 대상 map들의 key list로, 콤마(,)로 구분한다.
- \<lenkeys\>과 \<numkeys> - key list 문자열의 길이와 key 개수를 나타낸다.
- \<field_count\> - 조회할 field 개수를 지정
- \<"space separated fields"\> - 대상 map의 field list로, 띄어쓰기(" ")로 구분한다.

성공 시의 response string은 아래와 같다.
조회한 대상 key마다 VALUE 라인이 있으며, 대상 key string과 조회 상태가 나타난다.
조회 상태는 아래 중의 하나가 되며, 각 의미는 mop get 명령의 response string을 참조 바란다.

```
VALUE <key> <flags> <ecount>\r\n
[ELEMENT <field> <bytes> <data>\r\n
 ...
 ELEMENT <field> <bytes> <data>\r\n]
VALUE <key> <flags> <ecount>\r\n
[ELEMENT <field> <bytes> <data>\r\n
 ...
 ELEMENT <field> <bytes> <data>\r\n]

...

VALUE <key> <flags> <ecount>\r\n
[ELEMENT <field> <bytes> <data>\r\n
 ...
 ELEMENT <field> <bytes> <data>\r\n]
END\r\n
```
 
실패 시의 response string과 그 의미는 아래와 같다.

- “NOT_FOUND”	- key miss
- “NOT_FOUND_FIELD”	- field miss (field, element가 존재하지 않는 상태임)
- “TYPE_MISMATCH”	- 해당 item이 map collection이 아님
- “UNREADABLE” - 해당 item이 unreadable item임
- “CLIENT_ERROR bad command line format” - protocol syntax 틀림
- “CLIENT_ERROR bad data chunk”	- comma seperated key list의 길이가 \<lenkeys\>와 다르거나 “\r\n”으로 끝나지 않음
- "SERVER_ERROR out of memory [writing get response]”	- 메모리 부족

