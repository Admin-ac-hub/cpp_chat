#include "cpp_chat/app/chat_server_app.h"
#include "cpp_chat/core/server_config.h"

int main() {
    auto config = cpp_chat::core::load_default_config();
    cpp_chat::app::ChatServerApp app(config);
    return app.run();
}
