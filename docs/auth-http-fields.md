# 认证 HTTP 字段说明

最近同步时间：2026-03-14

## 1. 文档目的

这份文档专门解释当前认证 HTTP 接口里已经出现的请求头、请求体、响应体和常见字段含义。

它和 `docs/auth-http-api.md` 的分工如下：

- `docs/auth-http-api.md`：记录接口路径、示例、状态码和当前落地情况
- `docs/auth-http-fields.md`：解释字段作用、字段类型、是否必填、使用方式和注意事项

当前文档覆盖已经真实落地的认证接口：

- `POST /api/v1/auth/register`
- `POST /api/v1/auth/login`
- `POST /api/v1/auth/logout`

## 2. 整体结构

当前认证接口的交互结构可以拆成 4 部分：

1. 请求行  
   例如：`POST /api/v1/auth/register`
2. 请求头  
   例如：`Content-Type`、`X-Request-Id`
3. 请求体  
   当前使用 JSON 对象
4. 响应体  
   当前统一返回 `code / message / request_id / data`

## 3. 请求头字段

### 3.1 `Content-Type`

- 类型：HTTP Header
- 当前值：`application/json`
- 是否必填：是
- 作用：告诉服务端当前请求体是 JSON

说明：

- 当前认证接口只接受 JSON 请求体
- 如果请求体不是合法 JSON，服务端会返回 `400`

### 3.2 `X-Request-Id`

- 类型：HTTP Header
- 当前值：字符串
- 是否必填：否
- 作用：请求追踪号

说明：

- 如果客户端传了，服务端会原样回显到响应体里的 `request_id`
- 如果客户端没传，服务端会自己生成一个
- 它不是业务字段，不影响注册是否成功
- 它主要用于联调、查日志、定位一次请求的完整链路

推荐理解：

- `request_id` 解决的是“这一条日志、这一条响应、这一次请求是不是同一件事”
- 后续登录、refresh、文件上传、WebSocket 鉴权也可以继续沿用同样的追踪方式

### 3.3 `Authorization`

- 类型：HTTP Header
- 当前值：`Bearer <access_token>`
- 是否必填：登出接口必填
- 作用：携带当前访问令牌

说明：

- 当前 `POST /api/v1/auth/logout` 不使用请求体，而是直接从 `Authorization` 里读取 `access_token`
- 当前只支持 `Bearer` 方案
- 如果缺少这个请求头、格式不正确或 token 校验失败，服务端会返回 `40102`

## 4. 注册请求体字段

当前注册请求体是一个 JSON 对象，示例如下：

```json
{
  "account": "Wumo_01",
  "password": "Password123",
  "nickname": "Wumo",
  "avatar_upload_key": "tmp/2026/03/16/tmp_xxx.png"
}
```

### 4.1 `account`

- 类型：`string`
- 是否必填：是
- 作用：登录账号

当前规则：

- 长度必须在 3 到 64 之间
- 大小写敏感
- 不允许前后空格
- 只允许字母、数字、下划线、点和短横线

使用说明：

- 这是后续登录时使用的账号原值
- 当前服务端不会把它自动转成小写
- `Alice` 和 `alice` 当前会被视为两个不同账号

### 4.2 `password`

- 类型：`string`
- 是否必填：是
- 作用：登录密码原文

当前规则：

- 长度必须在 8 到 72 之间
- 当前不会自动 trim
- 前后空格如果输入了，会被当成密码内容的一部分

使用说明：

- 这个字段只在请求中短暂存在
- 服务端不会把明文密码直接写进数据库
- 服务端会先把它转成 bcrypt 哈希，再写入 `users.password_hash`

### 4.3 `nickname`

- 类型：`string`
- 是否必填：是
- 作用：用户展示昵称

当前规则：

- 服务端会先 trim 前后空格
- trim 后不能为空
- trim 后长度不能超过 64

使用说明：

- 它是展示字段，不是登录标识
- 当前注册成功后会原样返回在 `data.user.nickname`

### 4.4 `avatar_upload_key`

- 类型：`string`
- 是否必填：否
- 作用：临时头像上传成功后返回的确认 key

当前规则：

- 可以不传
- 可以传字符串
- 如果传了空字符串或只包含空白字符，当前服务端会按“未提供”处理
- 最大长度为 2048

使用说明：

- 它不是最终头像地址，而是临时头像确认 key
- 服务端会在注册成功时把它确认成正式头像，并把最终 `storage key` 写入 `users.avatar_url`
- 如果注册时没传，成功响应里当前也不会返回 `avatar_url`

## 5. 登录请求体字段

当前登录请求体也是一个 JSON 对象，示例如下：

```json
{
  "account": "Wumo_01",
  "password": "Password123",
  "device_id": "pc_windows_001",
  "device_platform": "desktop",
  "device_name": "Windows Desktop",
  "client_version": "0.1.0"
}
```

### 5.1 `device_id`

- 类型：`string`
- 是否必填：是
- 作用：客户端设备唯一标识

使用说明：

- 它决定这次登录最终落到哪一条 `device_session`
- 同一用户同一 `device_id` 只允许保留一条 `active` 会话
- 如果该设备已经登录当前账号，新的登录请求会被拒绝，而不是覆盖旧会话

### 5.2 `device_platform`

- 类型：`string`
- 是否必填：是
- 作用：设备平台标识

使用说明：

- 当前典型值可以是 `desktop`、`android`、`ios`
- 服务端会 trim 并统一转成小写
- 它主要用于设备管理展示、兼容性排查和后续风控

### 5.3 `device_name`

- 类型：`string`
- 是否必填：否
- 作用：设备展示名称

使用说明：

- 例如 `MacBook Pro`、`Windows Desktop`
- 主要用于设备管理页面展示
- trim 后为空会按未提供处理

### 5.4 `client_version`

- 类型：`string`
- 是否必填：否
- 作用：客户端版本号

使用说明：

- 主要用于兼容性排查
- trim 后为空会按未提供处理

### 5.5 登录里的 `account` 和 `password`

说明：

- 登录接口里的 `account` 含义和注册接口一致
- 登录接口里的 `password` 是明文校验输入，不会回传给客户端
- 当前登录只校验密码非空，不对密码复杂度做额外判断

## 6. 统一响应体字段

当前认证接口统一响应体结构如下：

```json
{
  "code": 0,
  "message": "ok",
  "request_id": "req_xxx",
  "data": {}
}
```

### 6.1 `code`

- 类型：`integer`
- 是否必有：是
- 作用：业务错误码

使用说明：

- `0` 表示业务成功
- 非 `0` 表示业务失败
- 它和 HTTP 状态码不是一回事

例如：

- HTTP `201` + `code=0`：注册成功
- HTTP `400` + `code=40001`：参数不合法
- HTTP `409` + `code=40901`：账号已存在

### 6.2 `message`

- 类型：`string`
- 是否必有：是
- 作用：本次请求的结果说明

使用说明：

- 成功时当前固定是 `ok`
- 失败时当前会返回适合直接展示或排查的错误文本
- 这个字段当前既承担“用户可读提示”，也承担“开发调试线索”

### 6.3 `request_id`

- 类型：`string`
- 是否必有：是
- 作用：本次请求的追踪 ID

使用说明：

- 它通常来自客户端的 `X-Request-Id`
- 如果客户端没传，则由服务端生成
- 当前客户端和服务端日志都应该围绕它做排查

### 6.4 `data`

- 类型：`object`
- 是否必有：是
- 作用：业务数据载荷

使用说明：

- 成功时，真正的返回数据放在 `data` 里
- 失败时，当前统一返回空对象
- 后续不同接口会在 `data` 下面继续放不同子对象

## 7. 注册成功响应字段

当前注册成功时，响应体中的 `data` 结构如下：

```json
{
  "user": {
    "user_id": "u_b2143de2e1264f2ead8ed79e",
    "account": "Wumo_01",
    "nickname": "Wumo",
    "avatar_url": "avatars/2026/03/16/avatar_xxx.png",
    "created_at_ms": 1773370203554
  }
}
```

### 7.1 `data.user`

- 类型：`object`
- 是否必有：是
- 作用：注册成功后返回的新用户基础信息

### 7.2 `data.user.user_id`

- 类型：`string`
- 是否必有：是
- 作用：用户业务主键

使用说明：

- 当前由服务端生成
- 格式类似 `u_xxx`
- 后续用户资料、好友关系、会话成员等都会围绕它建立关联

### 7.3 `data.user.account`

- 类型：`string`
- 是否必有：是
- 作用：注册成功后的账号原值

使用说明：

- 当前会原样回显用户注册时提交的账号
- 因为账号大小写敏感，所以回显原值有意义

### 7.4 `data.user.nickname`

- 类型：`string`
- 是否必有：是
- 作用：注册成功后的昵称

### 7.5 `data.user.avatar_url`

- 类型：`string`
- 是否必有：否
- 作用：头像 `storage key`

使用说明：

- 只有当注册时提供了 `avatar_upload_key` 时，这个字段才会出现在成功响应中

### 7.6 `data.user.created_at_ms`

- 类型：`integer`
- 是否必有：是
- 作用：账号创建时间的毫秒时间戳

使用说明：

- 当前以毫秒整数返回，方便客户端直接做排序、比较或显示格式化
- 它不是数据库原始时间字符串，而是已经转换过的时间戳

## 8. 登录成功响应字段

当前登录成功时，响应体中的 `data` 结构如下：

```json
{
  "user": {
    "user_id": "u_1001",
    "nickname": "Wumo",
    "avatar_url": "avatars/2026/03/16/avatar_xxx.png"
  },
  "device_session_id": "ds_7001",
  "access_token": "access_xxx",
  "refresh_token": "refresh_xxx",
  "expires_in_sec": 7200
}
```

### 8.1 `data.user`

- 类型：`object`
- 是否必有：是
- 作用：当前登录用户的基础信息

### 8.2 `data.device_session_id`

- 类型：`string`
- 是否必有：是
- 作用：本次登录对应的设备会话 ID

使用说明：

- 后续 refresh、WebSocket 鉴权、设备下线都围绕它工作
- 它不是用户 ID，也不是设备 ID，而是“这一次登录实例”的标识

### 8.3 `data.access_token`

- 类型：`string`
- 是否必有：是
- 作用：短期访问令牌

使用说明：

- 后续访问受保护的 HTTP 接口和 WebSocket `ws.auth` 都会用到它
- 当前登出接口 `POST /api/v1/auth/logout` 也直接使用它
- 当前服务端会把 `user_id`、`device_session_id` 和过期时间编码进 token，并附带签名

### 8.4 `data.refresh_token`

- 类型：`string`
- 是否必有：是
- 作用：刷新令牌

使用说明：

- 后续 refresh 接口会用它换新 access token
- 数据库里只保存它的哈希值，不保存原文

### 8.5 `data.expires_in_sec`

- 类型：`integer`
- 是否必有：是
- 作用：`access_token` 的有效时长

使用说明：

- 当前单位是秒
- 它表达的是“从现在开始还能用多久”，不是绝对时间戳

## 9. 登出接口字段

当前登出接口没有请求体，也没有额外业务响应字段。

### 9.1 登出请求

- 请求头里必须带 `Authorization: Bearer <access_token>`
- 当前不需要 JSON body

### 9.2 登出成功响应

- `code = 0`
- `message = "ok"`
- `data = {}`

使用说明：

- 登出成功后，服务端会把当前 `device_session` 更新为非活跃状态
- 当前只影响本次 `access_token` 对应的设备会话，不会下线其它设备

## 10. 失败响应字段

当前失败响应仍然沿用统一外层结构，只是 `code`、`message` 和 HTTP 状态码不同。

### 10.1 当前已落地的错误码

- `40000`
  - 含义：请求体不是合法 JSON

- `40001`
  - 含义：参数类型错误或业务校验失败

- `40102`
  - 含义：访问令牌缺失、损坏或格式非法

- `40101`
  - 含义：账号或密码错误

- `40301`
  - 含义：账号已被禁用

- `40302`
  - 含义：账号已被锁定

- `40901`
  - 含义：账号已存在

- `40902`
  - 含义：当前设备已登录该账号

- `50000`
  - 含义：服务端内部错误

### 10.2 失败时 `data`

- 类型：`object`
- 当前值：空对象
- 作用：保持响应结构稳定

说明：

- 当前失败时不会额外返回字段级错误列表
- 具体失败原因主要看 `code` 和 `message`

## 11. 字段使用建议

客户端当前建议这样使用这些字段：

- 以 HTTP 状态码判断请求大类是否成功
- 以 `code` 判断业务是否成功
- 以 `message` 作为失败提示或联调信息
- 以 `request_id` 作为日志追踪号
- 以 `data.user.*` 读取注册成功后的用户信息

服务端当前建议这样维护这些字段：

- 不要随意删除统一外层结构里的 `code / message / request_id / data`
- 新增字段时优先追加，不要轻易改变已有字段含义
- 错误码保持稳定，不要让同一错误在不同版本里反复改编号

## 12. 相关文档

- `docs/auth-http-api.md`
- `docs/chatclient-architecture.md`
- `docs/im-system-design.md`
