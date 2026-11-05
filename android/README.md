# Android

Compilación de prueba para Android 6 o posterior, con ARM64 y ARM de 32 bits.

El motor del juego se compila en C89. SDL2 proporciona la ventana, el audio y los eventos táctiles. GL4ES adapta el renderizado de OpenGL a OpenGL ES 2.0. Una actividad de Android prepara los recursos incluidos antes de iniciar el motor.

Para compilar en Windows se necesitan JDK 21, CMake, Ninja, Git, Android SDK Platform 35, Build Tools 35.0.0 y NDK r27c o posterior.

```powershell
.\build_android.ps1 -SdkPath C:\Android\Sdk -NdkPath C:\Android\Sdk\ndk\27.2.12479018
```

El script descarga revisiones fijadas de SDL2 y GL4ES, compila las dos arquitecturas y genera `app/build/outputs/apk/debug/app-debug.apk`, firmado para instalación de prueba.

Para generar únicamente ARM64, añade `-Abis arm64-v8a` al comando.

Usa los controles en pantalla para caminar e interactuar. Toca los diálogos para continuar. El botón PAUSA y el botón Atrás de Android abren la pausa. OBJETOS abre el inventario del capítulo 2.

El autoguardado y los tres espacios manuales quedan en el almacenamiento privado de la aplicación. El progreso se guarda al mandar el juego a segundo plano. Las licencias de las dependencias, tipografías y audios se incluyen dentro del APK.

Las dependencias se obtienen de sus proyectos oficiales. [SDL2](https://wiki.libsdl.org/SDL2/README-android), [GL4ES](https://github.com/ptitSeb/gl4es).
