#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
int main(int argc,char** argv) {
  std::vector<std::string> args(argv+1,argv+argc);
  bool json=false;std::string output;
  for(size_t i=0;i<args.size();i++) {if(args[i]=="--dump-single-json") json=true;if(args[i]=="--output"&&i+1<args.size()) output=args[i+1];}
  auto url=args.back();
  if(url.find("SLOW")!=std::string::npos) std::this_thread::sleep_for(std::chrono::seconds(5));
  if(url.find("ERROR")!=std::string::npos) {std::cerr<<"ERROR: Playlist is unavailable.\n";return 1;}
  if(json&&url.rfind("ytsearch5:",0)==0) {
    if(url.find("Missing")!=std::string::npos) std::cout<<R"JSON({"entries":[{"id":"Wrong_abc12","title":"Some other song","channel":"Other artist","duration":60}]})JSON";
    else if(url.find("Second")!=std::string::npos) std::cout<<R"JSON({"entries":[{"id":"Next_abc123","title":"Second","channel":"Test Artist - Topic","duration":60}]})JSON";
    else std::cout<<R"JSON({"entries":[{"id":"Wrong_abc12","title":"First (Karaoke)","channel":"Test Artist","duration":60},{"id":"Good_abc123","title":"First","channel":"Test Artist - Topic","duration":60}]})JSON";
    return 0;
  }
  if(json) {
    std::cout<<R"JSON({"title":"Test playlist","entries":[{"id":"Good_abc123","title":"Bass / colour","duration":1},{"id":"Bad__abc123","title":"Unavailable","duration":1},{"id":"Next_abc123","title":"Fine detail","duration":1},{"id":"Priv_abc123","title":"[Private video]"}]})JSON";
    return 0;
  }
  if(url.find("Bad__abc123")!=std::string::npos) {std::cerr<<"ERROR: Video unavailable\n";return 1;}
  for(size_t at=0;(at=output.find("%%",at))!=std::string::npos;at++) output.erase(at,1);
  std::ofstream file(output,std::ios::binary);file<<"cached audio fixture";return file?0:1;
}
