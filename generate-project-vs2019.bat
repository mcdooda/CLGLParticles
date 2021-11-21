rmdir /s /q .\project
mkdir project\Debug
mkdir project\Release
mkdir project\RelWithDebInfo
cd project
cmake .. -G "Visual Studio 16 2019"
cd ..
copy .\dll\* .\project\Debug
copy .\dll\* .\project\Release
copy .\dll\* .\project\RelWithDebInfo
copy .\CLGLParticles.vcxproj.user .\project
pause