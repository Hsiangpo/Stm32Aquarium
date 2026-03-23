# 交付产物目录

本目录用于存放可直接交付或安装的构建产物。

当前约定：

- `Aquarium_APP/entry-default-signed.hap`：HarmonyOS 应用签名包
- `Aquarium_APP/entry-default-unsigned.hap`：HarmonyOS 应用未签名包
- `Aquarium_APP/pack.info`：构建附带信息

推荐构建方式：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_aquarium_app.ps1
```

如果需要直接安装到本机 API 17 模拟器：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_aquarium_app.ps1 -Target 127.0.0.1:5555
```
