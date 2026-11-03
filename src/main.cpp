#include <iostream>
#include "httplib.h"
#include "router.h"
#include "system_ops.h"
#include "rfid_manager.h"
#include "newt_manager.h"

#define PORT 8000

int main() {
    // Initialize system operations and command wrapper
    system_ops::initialize();

    // RFID reader (I2C tab)
    rfid_manager::initialize();

    httplib::Server svr;

    // Register all routes defined in router.cpp
    setup_routes(svr);

    // Daily Newt update check (does nothing until Newt is set up, or if the
    // user turns automatic updates off in the Newt card)
    newt_manager::start_auto_updater();

    std::cout << "=================================================\n";
    std::cout << " Xerxes Pi Admin Backend\n";
    std::cout << " Listening on http://0.0.0.0:" << PORT << "\n";
    std::cout << "=================================================\n";

    svr.listen("0.0.0.0", PORT);

    return 0;
}
