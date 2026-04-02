# 用户资料与头像 HTTP 接口文档

最近同步时间：2026-03-16

## 1. 当前已落地

- `POST /api/v1/users/avatar/temp`
- `GET /api/v1/users/avatar/temp`
- `PATCH /api/v1/users/me/profile`
- `GET /api/v1/users/{user_id}/avatar`

说明：

- 这组接口负责“先上传临时头像，再确认写入用户资料”
- `users.avatar_url` 当前字段名继续沿用旧名字，但实际保存的是头像文件的 `storage key`
- 注册和资料修改都通过 `avatar_upload_key` 确认使用临时头像

## 2. 临时头像上传接口

- 方法：`POST`
- 路径：`/api/v1/users/avatar/temp`
- 作用：上传一个未绑定用户的临时头像文件，返回 `avatar_upload_key` 和可预览的 URL
- 鉴权：不需要登录
- 请求体：`multipart/form-data`

### 2.1 发送字段

表单示例：

```text
avatar=<图片文件>
```

表单字段说明：

- `avatar`
  - 类型：文件
  - 必填：是
  - 作用：要上传的头像图片文件

请求头说明：

- `X-Request-Id`
  - 可选
  - 作用：请求追踪号
  - 若客户端传入，服务端会原样回显到响应体里的 `request_id`

当前要求：

- 表单里至少包含一个图片文件
- 当前服务端会取第一个上传文件作为头像
- 头像文件大小当前不得超过 `5 MB`

### 2.2 成功响应

成功响应示例：

```json
{
  "code": 0,
  "message": "ok",
  "request_id": "req_avatar_temp_001",
  "data": {
    "avatar_upload_key": "tmp/2026/03/16/tmp_xxx.png",
    "preview_url": "/api/v1/users/avatar/temp?avatar_upload_key=tmp%2F2026%2F03%2F16%2Ftmp_xxx.png"
  }
}
```

返回字段说明：

- `avatar_upload_key`
  - 临时头像上传成功后的确认 key
  - 后续注册或修改资料时，通过它把临时头像转成正式头像

- `preview_url`
  - 临时头像预览地址
  - 主要给客户端注册页或资料页立即显示头像预览

## 3. 临时头像预览接口

- 方法：`GET`
- 路径：`/api/v1/users/avatar/temp`
- 作用：按 `avatar_upload_key` 返回临时头像文件
- 鉴权：不需要登录

### 3.1 发送字段

请求示例：

```text
GET /api/v1/users/avatar/temp?avatar_upload_key=tmp%2F2026%2F03%2F16%2Ftmp_xxx.png
```

查询参数说明：

- `avatar_upload_key`
  - 必填
  - 临时头像上传成功后返回的确认 key

### 3.2 返回

返回：

- 成功时直接返回图片文件
- 如果 `avatar_upload_key` 不存在或不合法，返回 `404` 或 `400`

## 4. 修改个人资料接口

- 方法：`PATCH`
- 路径：`/api/v1/users/me/profile`
- 作用：修改当前登录用户的可选资料字段
- 鉴权：需要 `Authorization: Bearer <access_token>`
- 请求体：`application/json`

### 4.1 发送字段

请求头说明：

- `Authorization: Bearer <access_token>`
  - 必填
  - 作用：标识当前登录用户

- `X-Request-Id`
  - 可选
  - 作用：请求追踪号
  - 若客户端传入，服务端会原样回显到响应体里的 `request_id`

请求体示例：

```json
{
  "nickname": "新昵称",
  "avatar_upload_key": "tmp/2026/03/16/tmp_xxx.png"
}
```

请求体字段说明：

- `nickname`
  - 类型：`string`
  - 必填：否
  - 作用：更新展示昵称

- `avatar_upload_key`
  - 类型：`string`
  - 必填：否
  - 作用：把临时头像确认成正式头像，并写入 `users.avatar_url`

请求约束：

- 至少要提交一个字段
- 当前不支持显式清空头像
- 如果提交了 `avatar_upload_key`，服务端会：
  1. 校验临时头像是否存在
  2. 把临时头像转成正式头像
  3. 更新 `users.avatar_url`
  4. 删除临时头像文件

### 4.2 成功响应

成功响应示例：

```json
{
  "code": 0,
  "message": "ok",
  "request_id": "req_profile_patch_001",
  "data": {
    "user": {
      "user_id": "u_1001",
      "account": "Wumo_01",
      "nickname": "新昵称",
      "avatar_url": "avatars/2026/03/16/avatar_xxx.png"
    }
  }
}
```

返回字段说明：

- `user.user_id`
  - 作用：当前用户 ID

- `user.account`
  - 作用：当前用户账号原值

- `user.nickname`
  - 作用：更新后的昵称

- `user.avatar_url`
  - 作用：更新后的头像 `storage key`
  - 客户端需要通过头像文件接口来真正获取图片内容

## 5. 获取指定用户头像接口

- 方法：`GET`
- 路径：`/api/v1/users/{user_id}/avatar`
- 作用：返回指定用户当前头像文件
- 鉴权：当前不需要登录

### 5.1 发送字段

请求示例：

```text
GET /api/v1/users/u_1001/avatar
```

路径参数说明：

- `user_id`
  - 必填
  - 要读取头像的用户 ID

### 5.2 返回

返回：

- 成功时直接返回图片文件
- 如果用户不存在或用户没有头像，返回 `404`

## 6. 与注册接口的关系

当前注册接口 `POST /api/v1/auth/register` 已支持可选字段：

- `avatar_upload_key`

推荐流程：

1. 先调用 `POST /api/v1/users/avatar/temp` 上传临时头像
2. 拿到 `avatar_upload_key`
3. 再调用 `POST /api/v1/auth/register`
4. 把 `avatar_upload_key` 一起提交
5. 注册成功后，服务端把临时头像转成正式头像，并把最终 `storage key` 写入 `users.avatar_url`

## 7. 手工测试

### 7.1 上传临时头像

```bash
curl -i -sS -X POST http://127.0.0.1:8848/api/v1/users/avatar/temp \
  -H 'X-Request-Id: req_avatar_temp_001' \
  -F 'avatar=@/tmp/chatserver-avatar-test.png'
```

### 7.2 注册时使用临时头像

```bash
curl -i -sS -X POST http://127.0.0.1:8848/api/v1/auth/register \
  -H 'Content-Type: application/json' \
  -H 'X-Request-Id: req_register_avatar_001' \
  -d '{
    "account":"AvatarUser_001",
    "password":"Password123",
    "nickname":"Avatar User",
    "avatar_upload_key":"tmp/2026/03/16/tmp_xxx.png"
  }'
```

### 7.3 更新资料时更换头像

```bash
curl -i -sS -X PATCH http://127.0.0.1:8848/api/v1/users/me/profile \
  -H 'Content-Type: application/json' \
  -H 'Authorization: Bearer access_xxx' \
  -H 'X-Request-Id: req_profile_patch_001' \
  -d '{
    "nickname":"Avatar User 2",
    "avatar_upload_key":"tmp/2026/03/16/tmp_xxx.png"
  }'
```

### 7.4 获取指定用户头像

```bash
curl -I -sS http://127.0.0.1:8848/api/v1/users/u_1001/avatar
```
