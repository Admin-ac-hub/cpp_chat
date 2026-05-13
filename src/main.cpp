#include "cpp_chat/app/chat_server_app.h"
#include "cpp_chat/core/server_config.h"

int main() {
    // 从默认值、config/mysql.env 和环境变量合并生成运行配置。
    auto config = cpp_chat::core::load_default_config();

    // 应用对象负责组装各个模块，main 只保留启动流程。
    cpp_chat::app::ChatServerApp app(config);
    return app.run();
}
