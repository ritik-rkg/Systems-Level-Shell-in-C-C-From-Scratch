#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
namespace fs = std::filesystem;
int echo(std::vector<std::string> v)
{
    for (int i = 1; i < v.size(); i++)
    {
        std::cout << v[i] << ((i == v.size() - 1) ? "" : " ");
    }
    std::cout << std::endl;
    return 1;
}

int myExit(std::vector<std::string> v)
{
    if (v.size() > 2)
    {
        return 0;
    }
    int code = stoi(v[1]);
    exit(code);
}

int type(std::vector<std::string> v)
{

    if (v.size() == 2)
    {
        std::cout << v[0] << " is " << v[1] << std::endl;
    }
    else if (v.size() == 1)
    {
        std::cout << v[0] << " is a shell builtin" << std::endl;
    }
    else
    {
        return 0;
    }
    return 1;
}

int pwd(vector<string> v)
{
    cout << fs::current_path().string() << endl;
    return 1;
}