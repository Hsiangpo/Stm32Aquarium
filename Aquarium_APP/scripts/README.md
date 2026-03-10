# Build Script

Use this script to build the HarmonyOS app in API17 mode without depending on system `java` in `PATH`.

## Command

```powershell
cd Aquarium_APP
powershell -ExecutionPolicy Bypass -File .\scripts\build_api17.ps1
```

## Optional

If DevEco Studio is installed in a custom location:

```powershell
$env:DEVECO_STUDIO_HOME = 'D:\\Your\\DevEco Studio'
powershell -ExecutionPolicy Bypass -File .\scripts\build_api17.ps1
```
