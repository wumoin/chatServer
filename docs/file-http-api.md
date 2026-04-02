# 文件 HTTP 协议文档

最近同步时间：2026-03-21

## 1. 文档目的

这份文档只记录当前已经真实落地的文件 HTTP 接口协议，覆盖：

- 上传聊天附件到服务端临时区
- 按正式附件 ID 下载附件

当前已落地接口：

- `POST /api/v1/files/upload`
- `GET /api/v1/files/{attachment_id}`

需要特别注意的一点：

- `POST /api/v1/files/upload` 当前返回的是临时 `attachment_upload_key`
- 这一步不会直接写 `attachments` 表
- 真正的正式附件确认，发生在后续 `message.send_image`、`message.send_file` 之类的业务动作里

## 2. 通用约定

### 2.1 路径前缀

- 当前文件接口统一走 `/api/v1/files/...`

### 2.2 鉴权

- 两条接口当前都要求已登录
- 请求头必须携带：
  - `Authorization: Bearer <access_token>`

### 2.3 响应结构

上传接口成功或失败时，统一返回：

```json
{
  "code": 0,
  "message": "ok",
  "request_id": "req_xxx",
  "data": {}
}
```

字段说明：

- `code`
  - 类型：`integer`
  - 作用：业务码，`0` 表示成功
- `message`
  - 类型：`string`
  - 作用：请求结果说明
- `request_id`
  - 类型：`string`
  - 作用：请求追踪 ID
- `data`
  - 类型：`object`
  - 作用：业务数据载荷；失败时当前统一返回空对象

下载接口成功时直接返回文件流，不走 JSON 包装。

## 3. 临时附件上传接口

### 3.1 接口信息

- 方法：`POST`
- 路径：`/api/v1/files/upload`
- 作用：把附件文件保存到服务端临时目录，并返回后续消息确认要用的 `attachment_upload_key`

### 3.2 当前语义

当前上传接口只负责“临时上传”，不负责“正式入库”：

- 上传成功后，文件会落到临时目录
- 服务端会同时写一份临时元数据 sidecar
- 这一步不会直接创建 `attachments.attachment_id`
- 客户端后续要把 `attachment_upload_key` 带到 `message.send_image` 或 `message.send_file`
- 只有消息正式确认成功后，服务端才会：
  - 把临时文件转到正式附件目录
  - 写入 `attachments` 表
  - 再由 `message.created` 把正式附件引用推给会话成员

当前上传实现和大小限制：

- 聊天附件上传大小上限统一为 `1 GB`
- 上传接口当前只粗分 `image` 和 `file` 两类：
  - 若上传 part 的 `Content-Type` 以 `image/` 开头，会归类成 `media_kind = image`
  - 其它任意 MIME / 扩展名组合都会归类成 `media_kind = file`
  - 因此普通附件当前并没有“只允许少数文件类型”的服务端白名单限制
- 客户端会在发起上传前先做一次本地前置校验
- 服务端 controller 在流式接收阶段会按累计字节数做一次上限拦截
- 服务端业务层也会再次校验大小，避免绕过 controller 直接调用 service 时出现口径漂移
- 服务端 `Drogon` 当前已经开启 `enable_request_stream = true`
- 服务端 `client_max_body_size` 已同步放宽到 `1G`
- 服务端 `client_max_memory_body_size` 当前保持为 `1M`
- 实际上传链路当前是：
  - controller 通过 multipart stream reader 边接收边解析 part
  - 文件 part 的字节会直接写入本地 staging file，而不是先整包拼进内存
  - 流结束后再把 `stagedFilePath + sizeBytes` 交给 `FileService`
  - `FileService` 再把 staging file 导入正式的“临时附件存储目录”，并写入 sidecar metadata
- 后续 `message.send_image` / `message.send_file` 把临时附件转正式附件时，也会直接从磁盘文件导入正式目录，不再把整份文件重新读回内存

### 3.3 请求字段

请求头：

- `Authorization: Bearer <access_token>`
- `Content-Type: multipart/form-data`
- `X-Request-Id`
  - 可选
  - 作用：请求追踪 ID

表单字段：

- `file`
  - 类型：文件
  - 必填：是
  - 作用：待上传的附件文件
- `image_width`
  - 类型：`integer`
  - 必填：否
  - 作用：图片宽度；客户端已知时可显式带上
- `image_height`
  - 类型：`integer`
  - 必填：否
  - 作用：图片高度；客户端已知时可显式带上

补充说明：

- 当前推荐统一使用字段名 `file`
- 当前实现并不强依赖字段名一定叫 `file`
- 只要 multipart 中最终只有一个文件 part，服务端就会把这个文件 part 当作上传内容
- 如果 multipart 中出现多个文件 part，服务端会返回 `only one file part is allowed`
- `image_width / image_height` 当前要求要么都带，要么都不带

### 3.4 返回字段

成功时返回：

- `data.upload`
  - 类型：`object`
  - 作用：本次临时上传的摘要信息
  - 字段：
    - `attachment_upload_key`
      - 类型：`string`
      - 作用：临时上传引用，后续 `message.send_image / message.send_file` 确认时要原样带回服务端
    - `file_name`
      - 类型：`string`
      - 作用：原始文件名
    - `mime_type`
      - 类型：`string`
      - 作用：服务端识别出的 MIME 类型
    - `size_bytes`
      - 类型：`int64`
      - 作用：文件大小，单位字节
    - `media_kind`
      - 类型：`string`
      - 作用：附件类型
      - 当前取值：
        - `image`
        - `file`
    - `image_width`
      - 类型：`int`
      - 作用：图片宽度
      - 可选：是
    - `image_height`
      - 类型：`int`
      - 作用：图片高度
      - 可选：是

### 3.5 成功示例

```json
{
  "code": 0,
  "message": "ok",
  "request_id": "req_file_upload_001",
  "data": {
    "upload": {
      "attachment_upload_key": "tmp/attachments/u_1001/2026/03/21/tmp_upload_xxx.png",
      "file_name": "design.png",
      "mime_type": "image/png",
      "size_bytes": 245120,
      "media_kind": "image",
      "image_width": 1280,
      "image_height": 720
    }
  }
}
```

### 3.6 测试用例

#### 用例 1：上传附件成功

```bash
curl -i -sS -X POST http://127.0.0.1:8848/api/v1/files/upload \
  -H 'Authorization: Bearer access_xxx' \
  -H 'X-Request-Id: req_file_upload_001' \
  -F 'file=@/tmp/design.png' \
  -F 'image_width=1280' \
  -F 'image_height=720'
```

预期结果：

- HTTP 状态码：`201 Created`
- `code = 0`
- `data.upload.attachment_upload_key` 有值

#### 用例 2：缺少文件字段

```bash
curl -i -sS -X POST http://127.0.0.1:8848/api/v1/files/upload \
  -H 'Authorization: Bearer access_xxx' \
  -H 'X-Request-Id: req_file_upload_002'
```

预期结果：

- HTTP 状态码：`400 Bad Request`
- `code = 40001`
- `message = "failed to parse multipart body"` 或 `message = "file is required"`

## 4. 正式附件下载接口

### 4.1 接口信息

- 方法：`GET`
- 路径：`/api/v1/files/{attachment_id}`
- 作用：按正式附件 ID 读取真实附件文件内容

### 4.2 请求字段

请求头：

- `Authorization: Bearer <access_token>`
- `X-Request-Id`
  - 可选
  - 作用：请求追踪 ID

路径参数：

- `attachment_id`
  - 类型：`string`
  - 必填：是
  - 作用：待下载附件的正式业务主键

补充说明：

- 这个接口只接受正式 `attachment_id`
- 临时 `attachment_upload_key` 当前不能直接拿来走下载接口

### 4.3 返回字段

成功时直接返回文件流：

- `Content-Type`
  - 作用：附件记录里的 `mime_type`
- `Content-Length`
  - 作用：文件大小，单位字节

当前成功响应不再包一层 JSON。

### 4.4 测试用例

#### 用例 1：下载附件成功

```bash
curl -i -sS http://127.0.0.1:8848/api/v1/files/att_xxx \
  -H 'Authorization: Bearer access_xxx' \
  -H 'X-Request-Id: req_file_download_001' \
  -o /tmp/downloaded_demo.txt
```

预期结果：

- HTTP 状态码：`200 OK`
- 响应头包含正确的 `Content-Type`
- 本地输出文件能成功写出

#### 用例 2：附件不存在

```bash
curl -i -sS http://127.0.0.1:8848/api/v1/files/att_not_exists \
  -H 'Authorization: Bearer access_xxx' \
  -H 'X-Request-Id: req_file_download_002'
```

预期结果：

- HTTP 状态码：`404 Not Found`
- `code = 40400`

## 5. 失败响应

### 5.1 access token 缺失或非法

- HTTP 状态码：`401 Unauthorized`
- `code`：`40102`
- `message`：`invalid access token`

### 5.2 multipart 上传体不合法

- HTTP 状态码：`400 Bad Request`
- `code`：`40001`
- `message` 可能是：
  - `failed to parse multipart body`
  - `file is required`
  - `only one file part is allowed`

### 5.3 临时上传业务校验失败

- HTTP 状态码：`400 Bad Request`
- `code`：`40001`
- `message` 可能是：
  - `file size must not exceed 1 GB`
  - `media kind must be 'image' or 'file'`
  - `image width and height must be both present or both absent`
  - `image attachment mime type must start with image/`
  - 普通文件上传当前不会因为扩展名不在预设列表里而被拒绝

### 5.4 正式附件不存在

- HTTP 状态码：`404 Not Found`
- `code`：`40400`
- `message` 可能是：
  - `attachment not found`
  - `attachment file not found`
