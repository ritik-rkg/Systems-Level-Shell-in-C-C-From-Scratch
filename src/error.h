#include <iostream>
#include <vector>
#include <string>
void error(std::vector<std::string> v)
{
    std::cout << v[0] << ": " << "not found" << std::endl;
}