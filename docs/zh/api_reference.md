# API指南<a name="ZH-CN_TOPIC_0000002552475173"></a>

## 规格约束<a name="ZH-CN_TOPIC_0000002521149976"></a>

- cache初始化传入容量大小，当前最大256TiB，不支持动态修改。
- `cache_line_size`支持`8k`/`16k`/`32k`/`64k`，推荐使用`8k`。
- ocf会创建io\_worker\_num个队列，一个io\_worker对应一个ocf队列对submission\_queue/completion\_queue。
- region\_id全局唯一，一个slot对应一个core，最多支持511个core，一个core逻辑空间最大4096TiB，最多承载128K个32GiB region。Slot下所有region放在slot对应core。设备空间下，region\_id重映射一个remap\_id，region在core上对应的区间为`remap_id*32GiB` ~ `(remap_id+1)*32GiB`。
- 同一个slot调用的`ocf_get`/`ocf_put`/`ocf_invalid`/`ocf_lookup`要求在一个线程中。

## 对外接口<a name="ZH-CN_TOPIC_0000002552349957"></a>

### 初始化<a name="ZH-CN_TOPIC_0000002521149978"></a>

**函数定义<a name="section1265691617129"></a>**

ocf模块初始化。接口类型为同步接口。

**实现方法<a name="section118471756151218"></a>**

```c
int ocf_init(struct ocf_config *cfg);
```

**参数说明<a name="section19368195513130"></a>**

| 参数名 | 数据类型                | 参数类型 | 描述               |
|-----|---------------------|------|------------------|
| cfg | struct ocf_config * | 入参   | 请参见[结构体章节](#结构体) |

**返回值<a name="section3542961712"></a>**

- `STATE_SUCCESS`：初始化成功。
- `STATE_PARAM_INVALID`：cfg内部参数非法，例如`cache_line_size`不是支持的类型。
- `STATE_FAIL`：ocf初始化失败，例如ocf已经进行过初始化；ocf上下文初始化/cache初始化等流程失败。

### 退出<a name="ZH-CN_TOPIC_0000002521309974"></a>

**函数定义<a name="section1641712238237"></a>**

ocf模块注销接口。接口类型为同步接口。

**实现方法<a name="section182041712122310"></a>**

```c
void ocf_exit();
```

**参数说明<a name="section66106413243"></a>**

无入参

**返回值<a name="section12583712162411"></a>**

无返回值

### 创建core<a name="ZH-CN_TOPIC_0000002552349959"></a>

**函数定义<a name="section1641712238237"></a>**

当前ocf core和slot一 一对应，新增slot时，通过该接口创建对应的ocf core资源。接口类型为异步接口。

**实现方法<a name="section182041712122310"></a>**

```c
int ocf_add_core(uint32_t slot_id);
```

**参数说明<a name="section66106413243"></a>**

| 参数名     | 数据类型     | 参数类型 | 描述          |
|---------|----------|------|-------------|
| slot_id | uint32_t | 入参   | slot标识，全局唯一 |

**返回值<a name="section12583712162411"></a>**

- `STATE_SUCCESS`：ocf core创建成功。
- `STATE_OCF_UNAVAILABLE`：ocf不可用，例如未进行初始化，或正在恢复中。
- `STATE_CORE_EXIST`：slot对应的core已经创建。
- `STATE_MEM_ALLOC_ERR`：内存不足或内存申请失败。
- `STATE_FAIL`：core创建流程失败。

### 删除core<a name="ZH-CN_TOPIC_0000002552229947"></a>

**函数定义<a name="section1641712238237"></a>**

当前ocf core和slot一 一对应，迁出或删除slot时，通过该接口释放对应的ocf core资源。释放时，ocf内部会对core在cache的映射做失效来确保slot在ocf中的缓存清除。接口类型为异步接口。

**实现方法<a name="section182041712122310"></a>**

```c
int ocf_remove_core(uint32_t slot_id);
```

**参数说明<a name="section66106413243"></a>**

| 参数名     | 数据类型     | 参数类型 | 描述          |
|---------|----------|------|-------------|
| slot_id | uint32_t | 入参   | slot标识，全局唯一 |

**返回值<a name="section12583712162411"></a>**

- `STATE_SUCCESS`：ocf core移除成功。
- `STATE_OCF_UNAVAILABLE`：ocf不可用，未进行初始化或正在做恢复中。
- `STATE_CORE_CREATING`：slot对应的core正在创建，不能删除。
- `STATE_MEM_ALLOC_ERR`：内存不足或内存申请失败。

### region cache失效<a name="ZH-CN_TOPIC_0000002552349961"></a>

**函数定义<a name="section1641712238237"></a>**

region删除时，调用该接口来清除cache中的region数据。接口类型为异步接口。

**实现方法<a name="section182041712122310"></a>**

```c
int ocf_remove_region(uint32_t slot_id, uint32_t region_id);
```

**参数说明<a name="section66106413243"></a>**

| 参数名（可选）   | 数据类型     | 参数类型 | 描述            |
|-----------|----------|------|---------------|
| slot_id   | uint32_t | 入参   | slot标识，全局唯一   |
| region_id | uint32_t | 入参   | region标识，全局唯一 |

**返回值<a name="section12583712162411"></a>**

- `STATE_SUCCESS`，数据清除成功。
- `STATE_OCF_UNAVAILABLE`，ocf不可用。
- `STATE_MEM_ALLOC_ERR`，申请失败。

### 提交读请求<a name="ZH-CN_TOPIC_0000002552229943"></a>

**函数定义<a name="section1641712238237"></a>**

将读请求提交到ocf队列，接口类型为异步接口。

请求说明：region指定区间数据在cache中读取。

**实现方法<a name="section182041712122310"></a>**

```c
int ocf_get(struct req_context *cxt);
```

**参数说明<a name="section66106413243"></a>**

| 参数名 | 数据类型                 | 参数类型 | 描述          |
|-----|----------------------|------|-------------|
| ctx | struct req_context * | 入参   | 请参见4.3结构体章节 |

**返回值<a name="section12583712162411"></a>**

- `STATE_SUCCESS`，请求提交ocf队列成功。
- `STATE_OCF_UNAVAILABLE`，ocf不可用。
- `STATE_PARAM_INVALID`，ctx为空指针或者ctx内部参数设置不合法。
- `STATE_CORE_NOT_EXIST`，slot对应core不存在，请求提交失败。
- `STATE_MEM_ALLOC_ERR`，内存申请失败。

### 提交写请求<a name="ZH-CN_TOPIC_0000002521309976"></a>

**函数定义<a name="section1641712238237"></a>**

将写请求提交到ocf队列，异步接口。

请求类型：将region指定区间数据刷新到cache。

**实现方法<a name="section182041712122310"></a>**

```c
int ocf_put(struct req_context *cxt);
```

**参数说明<a name="section66106413243"></a>**

| 参数名 | 数据类型                 | 参数类型 | 描述          |
|-----|----------------------|------|-------------|
| ctx | struct req_context * | 入参   | 请参见4.3结构体章节 |

**返回值<a name="section12583712162411"></a>**

- `STATE_SUCCESS`，请求提交ocf队列成功。
- `STATE_OCF_UNAVAILABLE`，ocf不可用。
- `STATE_PARAM_INVALID`，ctx为空指针或者ctx内部参数设置不合法。
- `STATE_CORE_NOT_EXIST`，slot对应core不存在，请求提交失败。
- `STATE_MEM_ALLOC_ERR`，内存申请失败。

### 提交失效请求<a name="ZH-CN_TOPIC_0000002521309968"></a>

**函数定义<a name="section1641712238237"></a>**

将失效请求提交到ocf队列，异步接口。

请求类型：region部分区间数据在cache中删除。

**实现方法<a name="section182041712122310"></a>**

```c
int ocf_invalid(struct req_context *ctx);
```

**参数说明<a name="section66106413243"></a>**

| 参数名 | 数据类型                 | 参数类型 | 描述          |
|-----|----------------------|------|-------------|
| ctx | struct req_context * | 入参   | 请参见4.3结构体章节 |

**返回值<a name="section12583712162411"></a>**

- `STATE_SUCCESS`，请求提交ocf队列成功。
- `STATE_OCF_UNAVAILABLE`，ocf不可用。
- `STATE_PARAM_INVALID`，ctx为空指针或者ctx内部参数设置不合法。
- `STATE_CORE_NOT_EXIST`，slot对应core不存在，请求提交失败。
- `STATE_MEM_ALLOC_ERR`，内存申请失败。

### 提交查询请求<a name="ZH-CN_TOPIC_0000002552349967"></a>

**函数定义<a name="section1641712238237"></a>**

将查询请求提交到ocf队列，异步接口。

请求类型：查询region指定区间数据在cache是否全命中。

**实现方法<a name="section182041712122310"></a>**

```c
int ocf_lookup(struct req_context *cxt);
```

**参数说明<a name="section66106413243"></a>**

| 参数名 | 数据类型                 | 参数类型 | 描述          |
|-----|----------------------|------|-------------|
| ctx | struct req_context * | 入参   | 请参见4.3结构体章节 |

**返回值<a name="section12583712162411"></a>**

- `STATE_SUCCESS`，请求提交ocf队列成功。
- `STATE_OCF_UNAVAILABLE`，ocf不可用。
- `STATE_PARAM_INVALID`，ctx为空指针或者ctx内部参数设置不合法。
- `STATE_CORE_NOT_EXIST`，slot对应core不存在，请求提交失败。
- `STATE_MEM_ALLOC_ERR`，内存申请失败。

### 轮询<a name="ZH-CN_TOPIC_0000002521309970"></a>

**函数定义<a name="section1641712238237"></a>**

触发已完成请求的回调，接口类型取决于执行回调接口的类型。

**实现方法<a name="section182041712122310"></a>**

```c
int ocf_poll(uint32_t io_worker_id, uint32_t max_num);
```

**参数说明<a name="section66106413243"></a>**

| 参数名          | 数据类型     | 参数类型 | 描述              |
|--------------|----------|------|-----------------|
| io_worker_id | uint32_t | 入参   | io_worker标识     |
| max_num      | uint32_t | 入参   | 每次可处理的请求结果的最大数量 |

**返回值<a name="section12583712162411"></a>**

- `STATE_SUCCESS`，请求提交ocf队列成功。
- `STATE_OCF_UNAVAILABLE`，ocf不可用。
- `STATE_PARAM_INVALID`，io\_worker\_id值异常。

### 获取ocf cache/core映射<a name="ZH-CN_TOPIC_0000002521149974"></a>

**函数定义<a name="section1641712238237"></a>**

获取ocf cache/core及其映射等信息，同步接口。

**实现方法<a name="section182041712122310"></a>**

```c
struct ocf_dump_info *ocf_dump_cache_core_info()；
```

**参数说明<a name="section66106413243"></a>**

无入参

**返回值<a name="section12583712162411"></a>**

- `NULL`，查询失败。
- `非NULL`，返回的结构体指针，结构体中包含查询信息的字符串，表示查询成功。

### 获取region\_id和remap\_id映射<a name="ZH-CN_TOPIC_0000002521309978"></a>

**函数定义<a name="section1641712238237"></a>**

获取region\_id和remap\_id的映射，同步接口。

**实现方法<a name="section182041712122310"></a>**

```c
struct ocf_dump_info *ocf_dump_region_info()；
```

**参数说明<a name="section66106413243"></a>**

无入参

**返回值<a name="section12583712162411"></a>**

- `NULL`，查询失败。
- `非NULL`，返回的结构体指针，结构体中包含查询信息的字符串，表示查询成功。

### 获取ocf内部处理IO的各项计数<a name="ZH-CN_TOPIC_0000002552349955"></a>

**函数定义<a name="section1641712238237"></a>**

ocf内部处理IO的各项计数，同步接口。

**实现方法<a name="section182041712122310"></a>**

```c
struct ocf_dump_info *ocf_dump_cache_stats()；
```

**参数说明<a name="section66106413243"></a>**

无入参

**返回值<a name="section12583712162411"></a>**

- `NULL`，查询失败。
- `非NULL`，返回的结构体指针，结构体中包含查询信息的字符串，表示查询成功。

### 查询ocf当前状态<a name="ZH-CN_TOPIC_0000002521149968"></a>

**函数定义<a name="section1641712238237"></a>**

查询ocf当前状态（用于确认ocf是否可用），同步接口。

**实现方法<a name="section182041712122310"></a>**

```c
struct ocf_dump_info *ocf_dump_status()；
```

**参数说明<a name="section66106413243"></a>**

无入参

**返回值<a name="section12583712162411"></a>**

- `NULL`，查询失败。
- `非NULL`，返回的结构体指针，结构体中包含查询信息的字符串，表示查询成功。

### 释放ocf\_dump\_info结构体内存<a name="ZH-CN_TOPIC_0000002521149972"></a>

**函数定义<a name="section1641712238237"></a>**

释放ocf\_dump\_info结构体内存，同步接口。

**实现方法<a name="section182041712122310"></a>**

```c
void ocf_release_dump_info(struct ocf_dump_info *info)；
```

**参数说明<a name="section66106413243"></a>**

| 参数名  | 数据类型                   | 参数类型 | 描述          |
|------|------------------------|------|-------------|
| info | struct ocf_dump_info * | 入参   | 请参见4.3结构体章节 |

**返回值<a name="section12583712162411"></a>**

无

## 结构体<a name="ZH-CN_TOPIC_0000002552349963" id="结构体"></a>

| 名称            | 说明                             | 定义                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
|---------------|--------------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| ocf_config    | ocf初始化入参结构体，包含ocf初始化必要的元素      | <pre><code>struct ocf_config {<br>&nbsp;&nbsp;uint64_t cache_capacity;    // 缓存大小，单位Byte，chunk大小整倍数<br>&nbsp;&nbsp;uint16_t io_worker_num;     // 外部会提交请求的线程的数量<br>&nbsp;&nbsp;uint16_t core_num;          // 分配给ocf线程使用的核数<br>&nbsp;&nbsp;uint64_t cache_line_size;   // 当前按8k<br>&nbsp;&nbsp;uint64_t chunk_pool_id;     // 用于cache存储数据的chunk层pool<br>&nbsp;&nbsp;uint128_t core_mask;        // 表示分配给ocf具体的核id集合<br>&nbsp;&nbsp;log_print_func log_print;   // 日志打印函数<br>};</code></pre>                                                                                |
| req_context   | 向ocf提交读/写/查询/失效这些异步请求的接口的入参结构体 | <pre><code>struct req_context {<br>&nbsp;&nbsp;void &#42;req_identifier;   //请求标识，索引到req上下文<br>&nbsp;&nbsp;uint32_t io_worker_id; // 索引sq<br>&nbsp;&nbsp;uint32_t slot_id;      // 索引ocf core<br>&nbsp;&nbsp;uint64_t region_id;    // 用于计算core offset<br>&nbsp;&nbsp;uint64_t offset;       // region内偏移，用于计算core offset<br>&nbsp;&nbsp;uint64_t len;          // 请求长度<br>&nbsp;&nbsp;char&#42; buffer;          // 读写请求使用的buffer<br>&nbsp;&nbsp;int (*cb)(int32_t ret, void*ctx); // 请求完成回调<br>&nbsp;&nbsp;char internal\[40\];       // ocf内部使用，无需设置<br>};</code></pre> |
| ocf_dump_info | 查询命令输出结构体                      | <pre><code>struct ocf_dump_info {<br>&nbsp;&nbsp;char &#42;buf;<br>&nbsp;&nbsp;size_t len;<br>};</code></pre>                                                                                                                                                                                                                                                                                                                                                                                                                                                              |

## 错误码<a name="ZH-CN_TOPIC_0000002521309966"></a>

| 枚举体描述 | 定义                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |
|-------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 错误码   | <pre><code>#define STATE_SUCCESS 0<br>/***error code for ocf processing result after request submission***/<br>#define STATE_FAIL -1<br>#define STATE_MISS -2<br>#define STATE_CHUNK_TIMEOUT -3<br>#define STATE_CHUNK_UNAVAILABLE -4<br>/***error code when submitting request***/<br>#define STATE_CORE_EXIST -1000<br>#define STATE_CORE_NOT_EXIST -1001<br>#define STATE_CORE_CREATING -1002<br>#define STATE_PARAM_INVALID -1003<br>#define STATE_MEM_ALLOC_ERR -1004<br>#define STATE_TOO_MANY_REGION -1005<br>#define STATE_OCF_UNAVAILABLE -1006</code></pre> |

## 修订记录
| 发布日期  | 修改说明       |
|-------|----------|
| 2024-06-30 | 第一次正式发布。 |