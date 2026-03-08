#include <iostream>
#define _WIN32_WINNT 0x0A00
#include "httplib.h"

int main() {
    httplib::Server server;

    server.Get("/api/jobs", [](const httplib::Request& req, httplib::Response& res) {
        res.set_content(
            R"([{"id": 1, "title": "Software Engineer", "company": "ACME"}])",
            "application/json"
        );
    });

    std::cout << "Server running on http://localhost:8080" << std::endl;
    server.listen("localhost", 8080);

    return 0;
}