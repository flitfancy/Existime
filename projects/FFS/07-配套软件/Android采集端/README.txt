FFS Android 采集端 · 0.2.0

保留原 Kotlin、Gradle、界面资源、Wi-Fi/BLE 协议、Room 队列与测试源码。
不包含 local.properties、Gradle/SDK/JDK 缓存、签名私钥、原设备凭证或缓存数据库。
原版说明目录是历史运行记录；其中的旧固件版本状态不代表本次重新实测。

构建环境：JDK 17 或更新的兼容版本、Gradle 8.13、Android SDK 36。
配置自己的 JAVA_HOME、ANDROID_HOME，并确保 gradle 在 PATH 中，然后运行：
  powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
也可用 -Gradle 指定 gradle.bat。脚本先把源码复制到新的英文临时目录，避免 Windows Android 工具的中文路径限制。
运行 testDebugUnitTest 和 assembleDebug，结果复制到被忽略的 build-output。
debug 构建不需要私人发布签名；debug APK 不能替代原发布签名更新已有安装。

App 默认承载 flitfancy.com，采集数据的配对与上传依赖相应网站后端。
需要自行搭建时，先按网站仓库说明部署，再调整 app/build.gradle.kts 中 WEBSITE_URL、UPLOAD_BASE 及 WebView 信任域名规则。
BLE_PROTOCOL.md 记录板端协议。手机锁屏、省电、网络切换和与手表并发等真机表现没有在本次源码整理中重新验收。

授权
FlitFancy 原创软件部分采用 MIT，见 LICENSE.txt 和 SOFTWARE-NOTICE.txt；上游来源及第三方许可证继续有效。
