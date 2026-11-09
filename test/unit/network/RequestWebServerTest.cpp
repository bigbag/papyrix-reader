#include <memory>
#include <utility>

#include "network/RequestWebServer.h"
#include "test_utils.h"

class ServerRequestFixture : public papyrix::RequestWebServer {
 public:
  ServerRequestFixture(size_t queryArguments, size_t formArguments) {
    if (queryArguments) {
      _currentArgs = new RequestArgument[queryArguments];
      _currentArgs[0].key = "path";
      _currentArgs[0].value = String(std::string(1023, 'x'));
    }
    if (formArguments) {
      _postArgs = new RequestArgument[formArguments];
      _postArgs[0].key = "name";
      _postArgs[0].value = String(std::string(255, 'x'));
    }
  }
};

int main() {
  TestUtils::TestRunner runner("HTTP request allocation lifetime");
  for (const auto& counts : {std::pair<size_t, size_t>{1, 0}, {4, 8}, {0, 8}}) {
    for (int cycle = 0; cycle < 8; ++cycle) {
      std::unique_ptr<WebServer> server(new ServerRequestFixture(counts.first, counts.second));
    }
    runner.expectTrue(WebServer::liveRequestArguments == 0,
                      "server restart cycles release all query and form arguments");
    if (!runner.allPassed()) return 1;
  }
  return runner.allPassed() ? 0 : 1;
}
