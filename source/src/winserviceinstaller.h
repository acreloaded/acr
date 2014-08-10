// minimal windows service installer

class winserviceinstaller
{

private:
    LPCSTR name;
    LPCSTR displayName;
    LPCSTR path;

    SC_HANDLE scm;

public:
    winserviceinstaller(LPCSTR name, LPCSTR displayName, LPCSTR path) : name(name), displayName(displayName), path(path), scm(nullptr)
    {
    }

    ~winserviceinstaller()
    {
        CloseManager();
    }

    bool OpenManger()
    {
        return ((scm = OpenSCManager(nullptr, nullptr, SC_MANAGER_ALL_ACCESS)) != nullptr);
    }

    void CloseManager()
    {
        if(scm) CloseServiceHandle(scm);
    }

    int IsInstalled()
    {
        if(!scm) return -1;
        SC_HANDLE svc = OpenService(scm, name, SC_MANAGER_CONNECT);
        bool installed = svc != nullptr;
        CloseServiceHandle(svc);
        return installed ? 1 : 0;
    }

    int Install()
    {
        if(!scm) return -1;
        SC_HANDLE svc = CreateService(scm, name, displayName, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL, path, nullptr, nullptr, nullptr, nullptr, nullptr);
        if(svc == nullptr) return 0;
        else
        {
            CloseServiceHandle(svc);
            return 1;
        }
    }
};

