# API Reference<a name="EN-US_TOPIC_0000002552475173"></a>

## Restrictions<a name="EN-US_TOPIC_0000002521149976"></a>

- The maximum capacity transferred during cache initialization is 256 TiB. The value cannot be dynamically changed.
- The value of `cache_line_size` can be `8k`, `16k`, `32k`, or `64k`. `8k` is recommended.
- OCF creates io\_worker\_num queues. One io\_worker corresponds to one OCF queue pair (submission\_queue/completion\_queue).
- region\_id of each region is globally unique. One slot corresponds to one core. A maximum of 511 cores are available. The maximum logical space of a core is 4,096 TiB, and can host a maximum of 128,000 32 GiB regions. All regions in a slot are placed in the core corresponding to the slot. In the device space, region\_id is remapped to remap\_id. The range of a region on the core is `remap_id × 32 GiB` to `(remap_id+1) × 32 GiB`.
- The `ocf_get`, `ocf_put`, `ocf_invalid`, and `ocf_lookup` interfaces called by the same slot must run in the same thread.

## External Interface<a name="EN-US_TOPIC_0000002552349957"></a>

### Initialization<a name="EN-US_TOPIC_0000002521149978"></a>

**Function Syntax<a name="section1265691617129"></a>**

Initialize the OCF module. This interface processes requests synchronously.

**Implementation Method<a name="section118471756151218"></a>**

```c
int ocf_init(struct ocf_config *cfg);
```

**Parameters<a name="section19368195513130"></a>**

| Parameter| Data Type               | Parameter Type| Description              |
|-----|---------------------|------|------------------|
| cfg | struct ocf_config * | Input  | For details, see [Structure](#Structure).|

**Return Value<a name="section3542961712"></a>**

- `STATE_SUCCESS`: The initialization is successful.
- `STATE_PARAM_INVALID`: The internal parameters of **cfg** are invalid. For example, `cache_line_size` is not supported.
- `STATE_FAIL`: OCF fails to be initialized. For example, OCF has been initialized, or OCF context initialization or cache initialization fails.

### Exiting the OCF Module<a name="EN-US_TOPIC_0000002521309974"></a>

**Function Syntax<a name="section1641712238237"></a>**

Exit the OCF module. This interface processes requests synchronously.

**Implementation Method<a name="section182041712122310"></a>**

```c
void ocf_exit();
```

**Parameters<a name="section66106413243"></a>**

None

**Return Value<a name="section12583712162411"></a>**

None

### Creating a Core<a name="EN-US_TOPIC_0000002552349959"></a>

**Function Syntax<a name="section1641712238237"></a>**

OCF core resources correspond to a new slot. Each ocf core corresponds to a unique slot. This interface processes requests asynchronously.

**Implementation Method<a name="section182041712122310"></a>**

```c
int ocf_add_core(uint32_t slot_id);
```

**Parameters<a name="section66106413243"></a>**

| Parameter    | Data Type    | Parameter Type| Description         |
|---------|----------|------|-------------|
| slot_id | uint32_t | Input  | Slot ID, which is globally unique|

**Return Value<a name="section12583712162411"></a>**

- `STATE_SUCCESS`: The OCF core is successfully created.
- `STATE_OCF_UNAVAILABLE`: OCF is unavailable, for example, it is not initialized or is being restored.
- `STATE_CORE_EXIST`: The core corresponding to the slot has been created.
- `STATE_MEM_ALLOC_ERR`: The memory is insufficient or memory allocation fails.
- `STATE_FAIL`: The core fails to create a process.

### Deleting a Core<a name="EN-US_TOPIC_0000002552229947"></a>

**Function Syntax<a name="section1641712238237"></a>**

The current OCF core corresponds to a slot. When a slot is migrated out or deleted, this interface is used to release the corresponding OCF core resources. During the release, OCF invalidates the mapping of the core in the cache to ensure that the slot cache in OCF is cleared. This interface processes requests asynchronously.

**Implementation Method<a name="section182041712122310"></a>**

```c
int ocf_remove_core(uint32_t slot_id);
```

**Parameters<a name="section66106413243"></a>**

| Parameter    | Data Type    | Parameter Type| Description         |
|---------|----------|------|-------------|
| slot_id | uint32_t | Input  | Slot ID, which is globally unique|

**Return Value<a name="section12583712162411"></a>**

- `STATE_SUCCESS`: The OCF core is successfully removed.
- `STATE_OCF_UNAVAILABLE`: OCF is unavailable because it is not initialized or is being restored.
- `STATE_CORE_CREATING`: The core corresponding to the slot is being created and cannot be deleted.
- `STATE_MEM_ALLOC_ERR`: The memory is insufficient or memory allocation fails.

### Invalidating Region Cache<a name="EN-US_TOPIC_0000002552349961"></a>

**Function Syntax<a name="section1641712238237"></a>**

When a region is deleted, this interface is called to clear region data in the cache. This interface processes requests asynchronously.

**Implementation Method<a name="section182041712122310"></a>**

```c
int ocf_remove_region(uint32_t slot_id, uint32_t region_id);
```

**Parameters<a name="section66106413243"></a>**

| Parameter (Optional)  | Data Type    | Parameter Type| Description           |
|-----------|----------|------|---------------|
| slot_id   | uint32_t | Input  | Slot ID, which is globally unique  |
| region_id | uint32_t | Input  | Region ID, which is globally unique.|

**Return Value<a name="section12583712162411"></a>**

- `STATE_SUCCESS`: The data is cleared successfully.
- `STATE_OCF_UNAVAILABLE`: OCF is unavailable.
- `STATE_MEM_ALLOC_ERR`: The application fails.

### Submitting a Read Request<a name="EN-US_TOPIC_0000002552229943"></a>

**Function Syntax<a name="section1641712238237"></a>**

Submit read requests to the OCF queue. This interface processes requests asynchronously.

Request description: Data in the specified region is read from the cache.

**Implementation Method<a name="section182041712122310"></a>**

```c
int ocf_get(struct req_context *cxt);
```

**Parameters<a name="section66106413243"></a>**

| Parameter| Data Type                | Parameter Type| Description         |
|-----|----------------------|------|-------------|
| ctx | struct req_context * | Input  | See section 4.3 "Structures."|

**Return Value<a name="section12583712162411"></a>**

- `STATE_SUCCESS`: The requests are successfully submitted to the OCF queue.
- `STATE_OCF_UNAVAILABLE`: OCF is unavailable.
- `STATE_PARAM_INVALID`: The ctx pointer is null, or its internal parameters are invalid.
- `STATE_CORE_NOT_EXIST`: The request fails to be submitted because the core corresponding to the slot does not exist.
- `STATE_MEM_ALLOC_ERR`: Memory allocation fails.

### Submitting a Write Request<a name="EN-US_TOPIC_0000002521309976"></a>

**Function Syntax<a name="section1641712238237"></a>**

Submit write requests to the OCF queue. This interface processes requests asynchronously.

Request type: Write data in the specified region range to the cache.

**Implementation Method<a name="section182041712122310"></a>**

```c
int ocf_put(struct req_context *cxt);
```

**Parameters<a name="section66106413243"></a>**

| Parameter| Data Type                | Parameter Type| Description         |
|-----|----------------------|------|-------------|
| ctx | struct req_context * | Input  | See section 4.3 "Structures."|

**Return Value<a name="section12583712162411"></a>**

- `STATE_SUCCESS`: The requests are successfully submitted to the OCF queue.
- <idp:inline displayname="code" id="code205144912391">STATE_OCF_UNAVAILABLE</idp:inline>: OCF is unavailable.
- `STATE_PARAM_INVALID`: The ctx pointer is null, or its internal parameters are invalid.
- `STATE_CORE_NOT_EXIST`: The request fails to be submitted because the core corresponding to the slot does not exist.
- `STATE_MEM_ALLOC_ERR`: Memory allocation fails.

### Submitting an Invalidation Request<a name="EN-US_TOPIC_0000002521309968"></a>

**Function Syntax<a name="section1641712238237"></a>**

Submit invalidation requests to the OCF queue. This interface processes requests asynchronously.

Request type: Remove data in the specified region range from the cache.

**Implementation Method<a name="section182041712122310"></a>**

```c
int ocf_invalid(struct req_context *ctx);
```

**Parameters<a name="section66106413243"></a>**

| Parameter| Data Type                | Parameter Type| Description         |
|-----|----------------------|------|-------------|
| ctx | struct req_context * | Input  | See section 4.3 "Structures."|

**Return Value<a name="section12583712162411"></a>**

- <idp:inline displayname="code" id="code9224937124116">STATE_SUCCESS</idp:inline>: The requests are successfully submitted to the OCF queue.
- <idp:inline displayname="code" id="code16134918398">STATE_OCF_UNAVAILABLE</idp:inline>: OCF is unavailable.
- <idp:inline displayname="code" id="code420724514412">STATE_PARAM_INVALID</idp:inline>: The ctx pointer is null, or its internal parameters are invalid.
- <idp:inline displayname="code" id="code10022184218">STATE_CORE_NOT_EXIST</idp:inline>: The request fails to be submitted because the core corresponding to the slot does not exist.
- <idp:inline displayname="code" id="code1973913711429">STATE_MEM_ALLOC_ERR</idp:inline>: Memory allocation fails.

### Submitting a Query Request<a name="EN-US_TOPIC_0000002552349967"></a>

**Function Syntax<a name="section1641712238237"></a>**

Submit query requests to the OCF queue. This interface processes requests asynchronously.

Request type: Query whether all data in the specified region range is hit in the cache.

**Implementation Method<a name="section182041712122310"></a>**

```c
int ocf_lookup(struct req_context *cxt);
```

**Parameters<a name="section66106413243"></a>**

| Parameter| Data Type                | Parameter Type| Description         |
|-----|----------------------|------|-------------|
| ctx | struct req_context * | Input  | See section 4.3 "Structures."|

**Return Value<a name="section12583712162411"></a>**

- <idp:inline displayname="code" id="code11224163744111">STATE_SUCCESS</idp:inline>: The requests are successfully submitted to the OCF queue.
- <idp:inline displayname="code" id="code13720494393">STATE_OCF_UNAVAILABLE</idp:inline>: OCF is unavailable.
- <idp:inline displayname="code" id="code162081545134118">STATE_PARAM_INVALID</idp:inline>: The ctx pointer is null, or its internal parameters are invalid.
- <idp:inline displayname="code" id="code8015214212">STATE_CORE_NOT_EXIST</idp:inline>: The request fails to be submitted because the core corresponding to the slot does not exist.
- <idp:inline displayname="code" id="code197404784211">STATE_MEM_ALLOC_ERR</idp:inline>: Memory allocation fails.

### Polling<a name="EN-US_TOPIC_0000002521309970"></a>

**Function Syntax<a name="section1641712238237"></a>**

Trigger the callback of a completed request. The interface type is subject to the type of the callback.

**Implementation Method<a name="section182041712122310"></a>**

```c
int ocf_poll(uint32_t io_worker_id, uint32_t max_num);
```

**Parameters<a name="section66106413243"></a>**

| Parameter         | Data Type    | Parameter Type| Description             |
|--------------|----------|------|-----------------|
| io_worker_id | uint32_t | Input  | io_worker ID    |
| max_num      | uint32_t | Input  | Maximum number of request results that can be processed each time|

**Return Value<a name="section12583712162411"></a>**

- <idp:inline displayname="code" id="code1022593764116">STATE_SUCCESS</idp:inline>: The requests are successfully submitted to the OCF queue.
- <idp:inline displayname="code" id="code071349183916">STATE_OCF_UNAVAILABLE</idp:inline>: OCF is unavailable.
- `STATE_PARAM_INVALID`: The value of io\_worker\_id is invalid.

### Obtaining the OCF Cache/Core Mapping<a name="EN-US_TOPIC_0000002521149974"></a>

**Function Syntax<a name="section1641712238237"></a>**

Obtain the OCF cache/core mapping information. This interface processes requests synchronously.

**Implementation Method<a name="section182041712122310"></a>**

```c
struct ocf_dump_info *ocf_dump_cache_core_info();
```

**Parameters<a name="section66106413243"></a>**

None

**Return Value<a name="section12583712162411"></a>**

- `NULL`: The query fails.
- `Non-NULL`: A structure pointer is returned. If the structure contains the character string of the queried information, the query is successful.

### Obtaining the Mapping Between region\_id and remap\_id<a name="EN-US_TOPIC_0000002521309978"></a>

**Function Syntax<a name="section1641712238237"></a>**

Obtain the mapping between region\_id and remap\_id. This interface processes requests synchronously.

**Implementation Method<a name="section182041712122310"></a>**

```c
struct ocf_dump_info *ocf_dump_region_info();
```

**Parameters<a name="section66106413243"></a>**

None

**Return Value<a name="section12583712162411"></a>**

- `NULL`: The query fails.
- `Non-NULL`: A structure pointer is returned. If the structure contains the character string of the queried information, the query is successful.

### Obtaining I/Os in OCF<a name="EN-US_TOPIC_0000002552349955"></a>

**Function Syntax<a name="section1641712238237"></a>**

Process I/O statistics in OCF. This interface processes requests synchronously.

**Implementation Method<a name="section182041712122310"></a>**

```c
struct ocf_dump_info *ocf_dump_cache_stats();
```

**Parameters<a name="section66106413243"></a>**

None

**Return Value<a name="section12583712162411"></a>**

- `NULL`: The query fails.
- `Non-NULL`: A structure pointer is returned. If the structure contains the character string of the queried information, the query is successful.

### Querying the OCF Status<a name="EN-US_TOPIC_0000002521149968"></a>

**Function Syntax<a name="section1641712238237"></a>**

Query the ocf status (to check whether OCF is available). This interface processes requests synchronously.

**Implementation Method<a name="section182041712122310"></a>**

```c
struct ocf_dump_info *ocf_dump_status();
```

**Parameters<a name="section66106413243"></a>**

None

**Return Value<a name="section12583712162411"></a>**

- `NULL`: The query fails.
- `Non-NULL`: A structure pointer is returned. If the structure contains the character string of the queried information, the query is successful.

### Releasing the ocf\_dump\_info Structure Memory<a name="EN-US_TOPIC_0000002521149972"></a>

**Function Syntax<a name="section1641712238237"></a>**

Release the ocf\_dump\_info structure memory. This interface processes requests synchronously.

**Implementation Method<a name="section182041712122310"></a>**

```c
void ocf_release_dump_info(struct ocf_dump_info *info);
```

**Parameters<a name="section66106413243"></a>**

| Parameter | Data Type                  | Parameter Type| Description         |
|------|------------------------|------|-------------|
| info | struct ocf_dump_info * | Input  | See section 4.3 "Structures."|

**Return Value<a name="section12583712162411"></a>**

None

## Structure<a name="EN-US_TOPIC_0000002552349963" id="Structure"></a>

| Name           | Description                            | Definition                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
|---------------|--------------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| ocf_config    | Input parameter structure, including necessary elements for ocf initialization     | <pre><code>struct ocf_config {<br>&nbsp;&nbsp;uint64_t cache_capacity; // Cache capacity, in bytes. The value is an integer multiple of the chunk size.<br>&nbsp;&nbsp;uint16_t io_worker_num; // Number of external threads that submit requests<br>&nbsp;&nbsp;uint16_t core_num; // Number of cores allocated to the OCF thread<br>&nbsp;&nbsp;uint64_t cache_line_size; // Currently, the value is 8 KB.<br>&nbsp;&nbsp;uint64_t chunk_pool_id; // Chunk pool used by the cache to store data<br>&nbsp;&nbsp;uint128_t core_mask; // Set of core IDs allocated to OCF<br>&nbsp;&nbsp;log_print_func log_print; // Log printing function<br>};</code></pre>                                                                                |
| req_context   | Input parameter structure of the interface for submitting asynchronous read/write/query/invalidation requests to OCF| <pre><code>struct req_context {<br>&nbsp;&nbsp;void &#42;req_identifier; //Request ID, which is indexed to the request context<br>&nbsp;&nbsp;uint32_t io_worker_id; // Index sq<br>&nbsp;&nbsp;uint32_t slot_id; // Index OCF core<br>&nbsp;&nbsp;uint64_t region_id; // Calculate the core offset.<br>&nbsp;&nbsp;uint64_t offset; // Offset in the region, which is used to calculate the core offset<br>&nbsp;&nbsp;uint64_t len; // Request length<br>&nbsp;&nbsp;char&#42; buffer;        // Buffer used for read/write requests<br>&nbsp;&nbsp;int (*cb)(int32_t ret, void*ctx); // Callback when a request is complete<br>&nbsp;&nbsp;char internal\[40\];      // OCF internal parameter that does not need to be set<br>};</code></pre> |
| ocf_dump_info | Query result structure                     | <pre><code>struct ocf_dump_info {<br>&nbsp;&nbsp;char &#42;buf;<br>&nbsp;&nbsp;size_t len;<br>};</code></pre>                                                                                                                                                                                                                                                                                                                                                                                                                                                              |

## Error Code<a name="EN-US_TOPIC_0000002521309966"></a>

| Enumeration| Definition                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
|-------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Error Code  | <pre><code>#define STATE_SUCCESS 0<br>/***error code for ocf processing result after request submission***/<br>#define STATE_FAIL -1<br>#define STATE_MISS -2<br>#define STATE_CHUNK_TIMEOUT -3<br>#define STATE_CHUNK_UNAVAILABLE -4<br>/***error code when submitting request***/<br>#define STATE_CORE_EXIST -1000<br>#define STATE_CORE_NOT_EXIST -1001<br>#define STATE_CORE_CREATING -1002<br>#define STATE_PARAM_INVALID -1003<br>#define STATE_MEM_ALLOC_ERR -1004<br>#define STATE_TOO_MANY_REGION -1005<br>#define STATE_OCF_UNAVAILABLE -1006</code></pre> |
