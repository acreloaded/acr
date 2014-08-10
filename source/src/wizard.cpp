// wizard to start an AssaultCube server and storing the configuration

#include "cube.h"

#ifdef WIN32
    #ifndef __GNUC__
        #pragma warning( disable : 4996 )
    #endif
    #include <direct.h>
    #include "winserviceinstaller.h"
#elif __GNUC__
    #include <sys/types.h>
    #include <sys/stat.h>
#endif

static void addarg(vect<char> &argstr, const char *name)
{
    argstr.add(' ');
    argstr.put(name, strlen(name));
}

static void addarg(vect<char> &argstr, const char *name, const char *val)
{
    addarg(argstr, name);
    bool space = strchr(val, ' ')!=nullptr;
    if(space) argstr.add('"');
    argstr.put(val, strlen(val));
    if(space) argstr.add('"');
}

static void readarg(const char *desc, char *val, int len)
{
    printf("%s: ", desc);
    fflush(stdout);
    fgets(val, len, stdin);
    char *end = strchr(val, '\n');
    if(end) *end = '\0';
}

static void readarg(vect<char> &argstr, const char *desc, const char *name)
{
    string val = "";
    readarg(desc, val, sizeof(val));
    if(val[0]) addarg(argstr, name, val);
}

int wizardmain(int argc, char **argv)
{
    const char *outfile = nullptr, *relpath = nullptr;
    for(int i = 1; i < argc; i++)
    {
        if(argv[i][0] == '-') continue;
        if(!outfile) outfile = argv[i];
        else if(!relpath) relpath = argv[i];
    }
    if(!outfile || !relpath)
    {
        printf("invalid arguments specified!\n");
        printf("usage: ac_server <outfile> <relbinarypath>\n");
        return EXIT_FAILURE;
    }

    printf("The AssaultCube Server Wizard\n\n");
    printf("Before setting up a new server, please ensure you've read the rules at:\n"
           "\thttp://ms.acr.victorz.ca\n\n");
    printf("You will also need to ensure that the UDP port you choose is open.\n"
           "Whatever port you choose, you will need to forward that port, plus one port after that.\n"
           "If you're having issues, use and forward the default ports %d and %d\n"
           "Use http://www.portforward.com for guidance.\n\n", CUBE_DEFAULT_SERVER_PORT, CUBE_SERVINFO_PORT(CUBE_DEFAULT_SERVER_PORT));
    printf("Now to specify some optional settings for your server.\n"
           "The default settings will be used if you leave the fields blank.\n"
           "If you're unsure about what to specify for the settings, leave the field blank.\n\n"
           "Read http://assault.cubers.net/docs/commandline.html for a description of these settings.\n\n");

    vect<char> argstr;
    readarg(argstr, "Server description", "-n");
    readarg(argstr, "Message of the day", "-o");
    readarg(argstr, "Maximum clients (No more than 16 allowed!)", "-c");
    readarg(argstr, "Administrator password", "-x");
    readarg(argstr, "Server port", "-f");

    printf("\nPrivate server settings:\n"
           "------------------------\n");
    string ispub = "";
    readarg("Public server (Yes/No)?", ispub, sizeof(ispub));
    if(toupper(ispub[0]) == 'N') addarg(argstr, "-mlocalhost");
    string cmds = "";
    readarg(argstr, "Player password", "-p");

    readarg("\nAdditional server switches", cmds, sizeof(cmds));
    if(cmds[0]) addarg(argstr, cmds);

#ifdef WIN32

    string wsname = "", wsdisplayname = "";
    readarg("win service name", wsname, sizeof(wsname));
    if(wsname[0])
        readarg("win service display", wsdisplayname, sizeof(wsdisplayname));

#endif

    printf("\nWriting your configuration to %s ... ", outfile); fflush(stdout);

    argstr.add('\0');

    FILE *script = fopen(outfile, "w");
    if(!script)
    {
        printf("Failed!\n");
        return EXIT_FAILURE;
    }

#ifdef WIN32
        fprintf(script, "%s%s\npause\n", relpath, argstr.getbuf());
#elif __GNUC__
        fprintf(script, "#!/bin/sh\n%s%s\n", relpath, argstr.getbuf());
#endif
    fclose(script);

    printf("Done\n\n");
    printf("Note: You can start %s directly the next time you want to use the same configuration to start the server.\n\n", outfile);

#ifdef WIN32

    if(wsname[0])
    {
        if(!wsdisplayname[0]) copystring(wsdisplayname, wsname);

        printf("Installing the ACR Server as windows service ... "); fflush(stdout);

        vect<char> path;
        databuf<char> cwd = path.reserveR(MAX_PATH);
        if(!_getcwd(cwd.buf, MAX_PATH))
        {
            printf("Failed!\n");
            printf("Could not get current working directory: %u\n", (uint)GetLastError());
            return EXIT_FAILURE;
        }
        path.advance(strlen(cwd.buf));
        path.add('\\');
        path.put(relpath, strlen(relpath));
        path.put(" -S", 3);
        path.put(wsname, strlen(wsname));
        path.add(' ');
        path.put(argstr.getbuf(), argstr.length());

        winserviceinstaller installer(wsname, wsdisplayname, path.getbuf());

        int r;
        if(!installer.OpenManger())
        {
            printf("Failed!\n");
            printf("Could not open the Service Control Manager: %u\n", (uint)GetLastError());
            installer.CloseManager();
            return EXIT_FAILURE;
        }

        if((r = installer.IsInstalled()) != 0)
        {
            printf("Failed!\n");
            if(r == -1) printf("Error accessing the Service Control Manager\n");
            else if(r == 1) printf("A windows service with this name (%s) is already installed: %u\n", wsname, (uint)GetLastError());
            return EXIT_FAILURE;
        }

        if((r = installer.Install()) != 1)
        {
            printf("Failed!\n");
            if(r == -1) printf("Error accessing the Service Control Manager\n");
            else if(r == 0) printf("Could not create the new windows service: %u\n", (uint)GetLastError());
            return EXIT_FAILURE;
        }

        printf("Done\n\n");
        printf("Note: You can now manage your ACR server using services.msc and sc.exe\n\n");
    }

#endif

    printf("Please press ENTER now to start your server...\n");
    fgetc(stdin);
    printf("Starting the ACR server ...\n");
    argstr.insert(0, relpath, strlen(relpath));
    system(argstr.getbuf());

    return EXIT_SUCCESS;
}
