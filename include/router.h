#pragma once
#include "httplib.h"

// Registers all HTTP routes onto the given server instance.
// Called once from main() before svr.listen() starts.
void setup_routes(httplib::Server& svr);
