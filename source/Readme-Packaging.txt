Source:
1. Subdirectory /source from SVN
2. Delete working files (/vcpp) except for files from SVN

Full Packages:
General Procedure:
1. Compile for the platforms
2. Delete unnecessary files: *.psd, *.ms3d, *.qc
3. Delete /source
4. Keep only the required /bin_*
5. Delete your /data
6. Delete the incorrect launch files from /

Modified steps:
Windows:
1. Compile with Visual Studio
6. Keep /*.bat

Linux:
1. a. See Readme-Linux-Compile.txt
   b. Replace native_ with linux_ or linux_64_
3. Do not delete /source
6. Keep /*.sh

Mac:
1. (compile it somehow!)
7. (probably more steps are required)