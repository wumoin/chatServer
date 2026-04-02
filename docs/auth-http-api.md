# 认证 HTTP 协议文档

最近同步时间：2026-03-16

## 1. 文档目的

这份文档只记录当前已经真实落地的认证 HTTP 接口协议，避免实现和设计文档长期漂移。

字段级解释见：

- `docs/auth-http-fields.md`

当前已落地：

- `POST /api/v1/auth/register`
- `POST /api/v1/auth/login`
- `POST /api/v1/auth/logout`

当前未落地：

- `POST /api/v1/auth/refresh`

## 2. 通用约定

### 2.1 路径前缀

- 当前认证接口统一走 `/api/v1/auth/...`

### 2.2 请求头

- `Content-Type: application/json`
- `X-Request-Id`：可选。若客户端传入，服务端会原样回显；若未传，服务端会自动生成

### 2.3 响应结构

当前认证接口统一返回：

```json
{
  "code": 0,
  "message": "ok",
  "request_id": "req_xxx",
  "data": {}
}
```

字段说明：

- `code`：业务码，`0` 表示成功
- `message`：当前请求结果说明
- `request_id`：请求追踪 ID
- `data`：业务数据载荷；失败时当前统一返回空对象

## 3. 注册接口

### 3.1 接口信息

- 方法：`POST`
- 路径：`/api/v1/auth/register`
- 作用：创建一个新的用户账号，并把用户基础资料写入 `users` 表

### 3.2 请求体

```json
{
  "account": "Wumo_01",
  "password": "Password123",
  "nickname": "Wumo",
  "avatar_upload_key": "tmp/2026/03/16/tmp_xxx.png"
}
```

字段说明：

- `account`
  - 类型：`string`
  - 必填：是
  - 作用：登录账号
  - 当前规则：
    - 长度 3 到 64
    - 大小写敏感
    - 不能有前后空格
    - 只允许字母、数字、`_`、`.`、`-`

- `password`
  - 类型：`string`
  - 必填：是
  - 作用：明文密码，由服务端转成 bcrypt 哈希后落库
  - 当前规则：
    - 长度 8 到 72
    - 当前不会 trim，前后空格会被视为密码内容本身

- `nickname`
  - 类型：`string`
  - 必填：是
  - 作用：展示昵称
  - 当前规则：
    - 服务端会先 trim 前后空格
    - trim 后长度必须在 1 到 64 之间

- `avatar_upload_key`
  - 类型：`string`
  - 必填：否
  - 作用：临时头像上传成功后返回的确认 key
  - 当前规则：
    - 若缺失或 trim 后为空，则按未提供处理
    - 最大长度 2048

### 3.3 成功响应

HTTP 状态码：

- `201 Created`

响应示例：

```json
{
  "code": 0,
  "message": "ok",
  "request_id": "req_register_001",
  "data": {
    "user": {
      "user_id": "u_b2143de2e1264f2ead8ed79e",
      "account": "Wumo_01",
      "nickname": "Wumo",
      "avatar_url": "avatars/2026/03/16/avatar_xxx.png",
      "created_at_ms": 1773370203554
    }
  }
}
```

返回字段说明：

- `user.user_id`
  - 作用：新创建用户的业务主键

- `user.account`
  - 作用：注册成功后的登录账号原值

- `user.nickname`
  - 作用：注册成功后的展示昵称

- `user.avatar_url`
  - 作用：头像 `storage key`；如果注册时没传则当前不会返回

- `user.created_at_ms`
  - 作用：账号创建时间，单位毫秒

### 3.4 失败响应

#### 请求体不是合法 JSON

- HTTP 状态码：`400 Bad Request`
- `code`：`40000`
- `message`：`invalid json body`

#### 参数类型错误或业务校验失败

- HTTP 状态码：`400 Bad Request`
- `code`：`40001`
- `message`：按具体校验原因返回，例如：
  - `Field 'account' must be a string`
  - `account must not contain leading or trailing spaces`
  - `password length must be between 8 and 72`

#### 账号已存在

- HTTP 状态码：`409 Conflict`
- `code`：`40901`
- `message`：`account already exists`

#### 服务端内部错误

- HTTP 状态码：`500 Internal Server Error`
- `code`：`50000`
- `message`：当前可能返回：
  - `failed to hash password`
  - `failed to create user`

## 4. 登录接口

### 4.1 接口信息

- 方法：`POST`
- 路径：`/api/v1/auth/login`
- 作用：校验账号密码，创建新的设备会话，并返回 `device_session_id + access_token + refresh_token`

### 4.2 请求体

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

字段说明：

- `account`
  - 类型：`string`
  - 必填：是
  - 作用：登录账号
  - 当前规则：
    - 长度 3 到 64
    - 大小写敏感
    - 不允许前后空格
    - 只允许字母、数字、`_`、`.`、`-`

- `password`
  - 类型：`string`
  - 必填：是
  - 作用：登录密码原文
  - 当前规则：
    - 只校验非空
    - 不自动 trim

- `device_id`
  - 类型：`string`
  - 必填：是
  - 作用：客户端设备唯一标识
  - 当前规则：
    - 不允许为空
    - 不允许前后空格
    - 最大长度 128
    - 如果同一用户同一 `device_id` 已存在 `active` 会话，则本次登录会被拒绝

- `device_platform`
  - 类型：`string`
  - 必填：是
  - 作用：设备平台标识，例如 `desktop / android / ios`
  - 当前规则：
    - 服务端会 trim 并转成小写
    - 长度 1 到 32
    - 只允许字母、数字、`_`、`-`

- `device_name`
  - 类型：`string`
  - 必填：否
  - 作用：设备展示名
  - 当前规则：
    - trim 后为空则按未提供处理
    - 最大长度 128

- `client_version`
  - 类型：`string`
  - 必填：否
  - 作用：客户端版本号
  - 当前规则：
    - trim 后为空则按未提供处理
    - 最大长度 32

### 4.3 成功响应

HTTP 状态码：

- `200 OK`

响应示例：

```json
{
  "code": 0,
  "message": "ok",
  "request_id": "req_login_001",
  "data": {
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
}
```

返回字段说明：

- `user.user_id`
  - 作用：当前登录用户 ID

- `user.nickname`
  - 作用：当前登录用户昵称

- `user.avatar_url`
  - 作用：当前登录用户头像 `storage key`；若为空则当前不会返回

- `device_session_id`
  - 作用：本次登录创建的设备会话 ID

- `access_token`
  - 作用：后续 HTTP / WebSocket 鉴权使用的短期访问令牌

- `refresh_token`
  - 作用：后续 refresh 接口续期使用的长期刷新令牌

- `expires_in_sec`
  - 作用：`access_token` 的有效时长，单位秒

### 4.4 失败响应

#### 请求体不是合法 JSON

- HTTP 状态码：`400 Bad Request`
- `code`：`40000`
- `message`：`invalid json body`

#### 参数类型错误或业务校验失败

- HTTP 状态码：`400 Bad Request`
- `code`：`40001`
- `message`：按具体原因返回，例如：
  - `Field 'device_id' must be a string`
  - `device_platform may contain only letters, digits, '_' and '-'`

#### 账号或密码错误

- HTTP 状态码：`401 Unauthorized`
- `code`：`40101`
- `message`：`invalid credentials`

#### 账号被禁用

- HTTP 状态码：`403 Forbidden`
- `code`：`40301`
- `message`：`account disabled`

#### 账号被锁定

- HTTP 状态码：`403 Forbidden`
- `code`：`40302`
- `message`：`account locked`

#### 当前设备已登录该账号

- HTTP 状态码：`409 Conflict`
- `code`：`40902`
- `message`：`device already logged in`

#### 服务端内部错误

- HTTP 状态码：`500 Internal Server Error`
- `code`：`50000`
- `message`：当前可能返回：
  - `failed to query user`
  - `failed to issue login tokens`

## 5. 登出接口

### 5.1 接口信息

- 方法：`POST`
- 路径：`/api/v1/auth/logout`
- 作用：根据当前 `access_token` 失效对应的 `device_session`

### 5.2 请求字段

当前登出接口不使用 JSON body，只使用请求头。

请求头示例：

```http
Authorization: Bearer access_xxx
X-Request-Id: req_logout_001
```

字段说明：

- `Authorization: Bearer <access_token>`
  - 必填
  - 作用：携带当前登录态 access token
  - 服务端会从中解析当前登录用户和当前 `device_session`

- `X-Request-Id`
  - 可选
  - 作用：请求追踪号
  - 若客户端传入，服务端会原样回显到响应体里的 `request_id`

说明：

- 当前登出接口不需要请求体
- 当前只会失效 `access_token` 对应的当前设备会话，不会影响其它设备
- 如果当前设备会话已经是非 `active` 状态，则仍按幂等成功处理

### 5.3 成功响应

HTTP 状态码：

- `200 OK`

响应示例：

```json
{
  "code": 0,
  "message": "ok",
  "request_id": "req_logout_001",
  "data": {}
}
```

### 5.4 失败响应

#### 缺少或损坏的访问令牌

- HTTP 状态码：`401 Unauthorized`
- `code`：`40102`
- `message`：`invalid access token`

#### 服务端内部错误

- HTTP 状态码：`500 Internal Server Error`
- `code`：`50000`
- `message`：当前可能返回：
  - `failed to revoke device session`

## 6. 如何测试

下面的测试步骤默认在项目根目录执行：

```bash
cd /home/wumo/myproject/universty
```

### 6.1 启动前准备

第一次测试前，先确认数据库表已经初始化：

```bash
python3 chatServer/db/init_sql.py
```

然后编译并启动服务端：

```bash
cmake -S chatServer -B tmpbuild/chatServer
cmake --build tmpbuild/chatServer -j2
./tmpbuild/chatServer/chatServer
```

如果服务正常启动，可以先确认健康检查：

```bash
curl -sS http://127.0.0.1:8848/health
```

### 6.2 测试注册成功

建议每次测试使用一个新的账号，避免被唯一约束影响。

```bash
curl -i -sS -X POST http://127.0.0.1:8848/api/v1/auth/register \
  -H 'Content-Type: application/json' \
  -H 'X-Request-Id: req_register_manual_001' \
  -d '{
    "account":"AuthManual_001",
    "password":"Password123",
    "nickname":"Manual User"
  }'
```

预期结果：

- HTTP 状态码：`201 Created`
- `code = 0`
- `data.user.user_id` 有值

### 6.3 测试重复账号

对同一个账号再次发注册请求：

```bash
curl -i -sS -X POST http://127.0.0.1:8848/api/v1/auth/register \
  -H 'Content-Type: application/json' \
  -H 'X-Request-Id: req_register_manual_002' \
  -d '{
    "account":"AuthManual_001",
    "password":"Password123",
    "nickname":"Manual User"
  }'
```

预期结果：

- HTTP 状态码：`409 Conflict`
- `code = 40901`
- `message = "account already exists"`

### 6.4 测试登录成功

先确保上一步的注册成功，然后对同一个账号发登录请求：

```bash
curl -i -sS -X POST http://127.0.0.1:8848/api/v1/auth/login \
  -H 'Content-Type: application/json' \
  -H 'X-Request-Id: req_login_manual_001' \
  -H 'User-Agent: manual-test/1.0' \
  -d '{
    "account":"AuthManual_001",
    "password":"Password123",
    "device_id":"pc_manual_001",
    "device_platform":"desktop",
    "device_name":"Manual Desktop",
    "client_version":"0.1.0"
  }'
```

预期结果：

- HTTP 状态码：`200 OK`
- `code = 0`
- `data.device_session_id` 有值
- `data.access_token` 有值
- `data.refresh_token` 有值
- `data.expires_in_sec` 有值

### 6.5 测试密码错误

```bash
curl -i -sS -X POST http://127.0.0.1:8848/api/v1/auth/login \
  -H 'Content-Type: application/json' \
  -H 'X-Request-Id: req_login_manual_002' \
  -d '{
    "account":"AuthManual_001",
    "password":"WrongPassword123",
    "device_id":"pc_manual_001",
    "device_platform":"desktop"
  }'
```

预期结果：

- HTTP 状态码：`401 Unauthorized`
- `code = 40101`
- `message = "invalid credentials"`

### 6.6 测试同设备重复登录

```bash
curl -i -sS -X POST http://127.0.0.1:8848/api/v1/auth/login \
  -H 'Content-Type: application/json' \
  -H 'X-Request-Id: req_login_manual_003' \
  -d '{
    "account":"AuthManual_001",
    "password":"Password123",
    "device_id":"pc_manual_001",
    "device_platform":"desktop"
  }'
```

预期结果：

- 如果同一账号同一设备已经存在活跃会话，则返回 `409 Conflict`
- `code = 40902`
- `message = "device already logged in"`

### 6.7 测试参数错误

```bash
curl -i -sS -X POST http://127.0.0.1:8848/api/v1/auth/login \
  -H 'Content-Type: application/json' \
  -H 'X-Request-Id: req_login_manual_003' \
  -d '{
    "account":"AuthManual_001",
    "password":"Password123",
    "device_id":" pc_manual_001 ",
    "device_platform":"desktop"
  }'
```

预期结果：

- HTTP 状态码：`400 Bad Request`
- `code = 40001`
- `message` 会提示 `device_id` 含前后空格或字段不合法

### 6.8 用数据库核对登录结果

如果要确认登录真的写入了设备会话，可以直接查库：

```bash
psql 'host=/var/run/postgresql port=5432 dbname=chatserver user=wumo password=123456' \
  -c "select u.account, ds.device_session_id, ds.device_id, ds.device_platform, ds.session_status, ds.login_user_agent from device_sessions ds join users u on u.user_id = ds.user_id where u.account = 'AuthManual_001';"
```

预期结果：

- 能查到一条 `session_status = active` 的记录
- `device_id` 和 `device_platform` 与登录请求一致
- `login_user_agent` 能看到 `manual-test/1.0`

### 6.9 测试登出成功

先把登录成功响应中的 `access_token` 复制出来，再执行：

```bash
curl -i -sS -X POST http://127.0.0.1:8848/api/v1/auth/logout \
  -H 'Authorization: Bearer access_xxx' \
  -H 'X-Request-Id: req_logout_manual_001'
```

预期结果：

- HTTP 状态码：`200 OK`
- `code = 0`
- `message = "ok"`

再次查库：

```bash
psql 'host=/var/run/postgresql port=5432 dbname=chatserver user=wumo password=123456' \
  -c "select u.account, ds.device_session_id, ds.session_status, ds.revoke_reason from device_sessions ds join users u on u.user_id = ds.user_id where u.account = 'AuthManual_001' order by ds.created_at desc;"
```

预期结果：

- 最新一条会话的 `session_status = revoked`
- `revoke_reason = logout`

### 6.10 测试非法 access token

```bash
curl -i -sS -X POST http://127.0.0.1:8848/api/v1/auth/logout \
  -H 'Authorization: Bearer invalid_token' \
  -H 'X-Request-Id: req_logout_manual_002'
```

预期结果：

- HTTP 状态码：`401 Unauthorized`
- `code = 40102`
- `message = "invalid access token"`

### 6.11 清理测试数据

如果不想保留测试账号，可以执行：

```bash
psql 'host=/var/run/postgresql port=5432 dbname=chatserver user=wumo password=123456' \
  -c "delete from device_sessions where user_id in (select user_id from users where account = 'AuthManual_001'); delete from users where account = 'AuthManual_001';"
```

## 7. 当前实现说明

当前注册接口对应实现位置：

- `chatServer/src/transport/http/auth_controller.*`
- `chatServer/src/service/auth_service.*`
- `chatServer/src/repository/user_repository.*`
- `chatServer/src/infra/id/*`
- `chatServer/src/infra/security/*`
- `chatClient/src/dto/auth_dto.*`
- `chatClient/src/api/auth_api_client.*`
- `chatClient/src/service/auth_service.*`
- `chatClient/src/qt_widget/loginwindow.*`

当前落库表：

- `users`
- `device_sessions`

当前登录接口对应实现位置：

- `chatServer/src/protocol/dto/auth/login_dto.h`
- `chatServer/src/transport/http/auth_controller.*`
- `chatServer/src/service/auth_service.*`
- `chatServer/src/repository/user_repository.*`
- `chatServer/src/repository/device_session_repository.*`
- `chatServer/src/infra/id/*`
- `chatServer/src/infra/security/password_hasher.*`
- `chatServer/src/infra/security/token_provider.*`

当前登出接口对应实现位置：

- `chatServer/src/transport/http/auth_controller.*`
- `chatServer/src/service/auth_service.*`
- `chatServer/src/repository/device_session_repository.*`
- `chatServer/src/infra/security/token_provider.*`

当前已接入：

- 登录后返回 `device_session_id`
- 登录后返回 `access_token`
- 登录后返回 `refresh_token`
- 登录成功时写入 `device_sessions`
- 已支持用 `Authorization: Bearer <access_token>` 失效当前设备会话

当前还没有接入：

- refresh 接口
- 注册成功后自动登录
