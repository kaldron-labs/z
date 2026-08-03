#include <zlib/ZrlCLI.hh>
#include <zlib/ZrlHistory.hh>
#include <zlib/ZrlGlobber.hh>

int main()
{
  Zrl::Globber globber;
  Zrl::History history{100};
  Zrl::CLI cli;
  cli.init(Zrl::App{
    .error = [](ZuCSpan s) { std::cerr << s << '\n'; },
    .prompt = [](Zrl::Prompt &s) { if (!s) s = "-->] "; },
    .enter = [](ZuCSpan s) -> bool {
      std::cout << s << '\n';
      return s == "quit";
    },
    .sig = [](int sig) -> bool {
      switch (sig) {
	case SIGINT: std::cout << "SIGINT\n"; break;
	case SIGQUIT: std::cout << "SIGQUIT\n"; break;
	case SIGTSTP: std::cout << "SIGTSTP\n"; break;
      }
      return false;
    },
    .compInit = globber.initFn(),
    .compStart = globber.startFn(),
    .compSubst = globber.substFn(),
    .compNext = globber.nextFn(),
    .histSave = history.saveFn(),
    .histLoad = history.loadFn()
  });
  if (!cli.open()) {
    std::cerr << "failed to open terminal\n";
    ::exit(1);
  }
  std::cout << cli.dumpVKeys();
  std::cout << cli.dumpMaps();
  cli.start();
  cli.join();
  cli.stop();
  cli.close();
  cli.final();
}
