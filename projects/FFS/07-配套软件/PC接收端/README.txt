FFS Windows TCP 接收端

在此目录运行：
  powershell -NoProfile -ExecutionPolicy Bypass -File .\start-listener.ps1
默认监听 TCP 7777，写入本目录 data/sensors；默认不转发网站。
板端 EEPROM 中的 TCP 主机地址应指向运行接收端的电脑。
需要更换端口或输出目录时使用 -Port 和 -DataRoot。
需要接入本机 FlitFancy 后端时增加 -ForwardToFlitFancy。

scripts/vendor/listen_wifi.ps1 为现有接收器，保留 HELLO、PING/PONG、表头变化、断线重连、会话归档和 CSV 写入逻辑。
scripts/watch_sensor_listener.ps1 为可选守护脚本，需要显式给出 ListenerPath；PID 保存在选定 DataRoot 中。
tests 中两个脚本只用临时目录和临时本机端口验证存储与重连，不连接真实板子。
data、logs、PID 和日志均不随源码分享。

授权
FlitFancy 原创软件部分采用 MIT，见 LICENSE.txt 和 SOFTWARE-NOTICE.txt；上游来源及第三方许可证继续有效。
