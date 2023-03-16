#include <yaml-cpp/yaml.h>
#include <string>

#include "webserver/webserver.h"
#include "global_definition/global_definition.h"

int main(int argc, char* argv[])
{
    std::string config_file_path = WORK_SPACE_PATH + "/config/config.yaml";
    YAML::Node config_node = YAML::LoadFile(config_file_path);

    WebServer webserver;
    webserver.run(config_node);
    
    return 0;
}