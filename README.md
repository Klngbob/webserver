## Linux 下轻量级Web 服务器
Linux 下的高性能、多线程并发Web 服务器，支持GET、POST 请求，可承受上万并发的连接

- 使用线程池 + 非阻塞socket + epoll + Proactor 事件处理模式的并发模型。
- 使用状态机解析HTTP 报文，支持GET、POST 请求。
- 通过数据库连接池访问服务器数据库实现Web 端用户登录、注册功能，可以请求图片和视频等大文件。
- 使用RAII 机制优化数据库连接的获取和释放，使用CGI 校验用户登录。
- 实现同步及异步日志系统、记录服务器运行状态。
- 使用最小堆定时器处理非活动连接。
- 使用Webbench 压力测试实现上万并发数据连接。

```sh
# 编译
./build.sh
# 运行
./bin/webserver
```

`config/config.yaml`提供参数配置。

[demo演示](http://47.101.165.85:12345/)
