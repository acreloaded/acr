Source:
1. Subdirectory /source from SVN
2. Delete working files (/vcpp) except for files from SVN

Server Pack:
1. Prepare a Linux Package, but modify some steps:
  3' Delete /bin_* except for
    - /bin_win32/ac_server.exe
    - /bin_win32/zlib1.dll
    - /bin_linux/linux_server
    - /bin_linux/linux_64_server
  7' Only delete the non-server launch scripts
2. Delete all client-only files:
    - /bot
    - /config (except for /config/default.*.cfg)
    - /docs
    - /locale
    - /packages (except /packages/maps)

Full Packages:
General Procedure:
1. Compile for the platforms
2. Delete /source
3. Keep only the required /bin_*
4. Delete the incorrect launch files from /
5. Delete your /data
6. Delete unnecessary files: *.psd, *.ms3d, *.qc
7. Clear /packages/maps/servermaps

Modified steps:
Windows:
1. Compile with Visual Studio
3. Keep /*.bat

Linux:
1. a. See Readme-Linux-Compile.txt
   b. Replace native_ with linux_ or linux_64_
2. Do not delete /source
3. Keep /*.sh

Mac:
1. (compile it somehow!)
8. (probably more steps are required)