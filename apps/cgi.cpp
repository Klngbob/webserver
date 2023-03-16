#include <mysql/mysql.h>
#include <iostream>
#include <string>
#include <string.h>
#include <map>
#include <fstream>
#include <sstream>
#include "lock/locker.h"

int main(int argc, char* argv[])
{
    
    std::map<std::string, std::string> m_users;

    locker m_mutex;

    std::ifstream out(argv[2]);
    std::string linestr;
    while(getline(out, linestr))
    {
        std::string str;
        std::stringstream id_passwd(linestr);

        getline(id_passwd, str, ' ');
        std::string tmp1(str);

        getline(id_passwd, str, ' ');
        std::string tmp2(str);

        m_users[tmp1] = tmp2;
    }
    out.close();

    std::string user(argv[0]);
    std::string passwd(argv[1]);

    if(m_users.find(user) != m_users.end() && m_users[user] == passwd)
    {
        printf("1\n");
    }
    else
    {
        printf("0\n");
    }

    // return 0;
}