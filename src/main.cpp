/* Chess-GUI-for-UCI — entry point. GPL-2.0, (C) 2025 Iman Zamani */
#include "gui.hpp"
#include <cstdio>
int main(){
    App app;
    if (!app.init()){
        fprintf(stderr, "Initialization failed. Ensure the Resources folder is next to the executable.\n");
        return 1;
    }
    app.run();
    return 0;
}
