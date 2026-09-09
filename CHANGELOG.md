# Changelog

## [2.4.1] - 2026-08-11

### 新增
- discovery_mgr UDP 广播的监听与发送地址/端口支持通过配置文件自定义 (`/discovery_mgr/udp/ipv4`、`/discovery_mgr/udp/ipv6`)
- HTTP 访问日志打印请求 Content-Type: 文本类(如 json/text)请求打印完整内容, 二进制请求仅打印长度
- HTTP 访问日志增加请求来源 IP 打印 (`req.remote_addr`)
- HTTP 访问日志: 打印每个请求的方法、路径、响应状态码与请求体
- musl 静态构建脚本 `ci/build-musl.sh`

### 变更
- 重构 CI 配置: tag push 触发完整 pipeline (build + publish + release), 支持 x86_64 (docker) 与 arm64 (shell executor) 双架构构建

## [2.3.0] - 2026-01-27

### 新增
- 读取和安装v2 包格式的能力


## [2.2.0] - 2026-01-13

### 新增
- webui 挂载点, 将网页放置于 ABILITY_FRAMEWORK_HOME/www 目录, 通过浏览器访问 /ui 路径

## [2.1.0] - 2025-12-11

### 新增
- 新版本DeviceCR及其api 

### 去除
- 去除 RedisMgr, 如今框架编译不再依赖redis, 运行时不使用redis

### 修复
- dump 默认配置文件后, source_urls为空的问题
