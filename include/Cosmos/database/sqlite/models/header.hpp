#include <string>

struct Header {
    int id; // height
    int version;
    std::string previous;
    std::string merkle_root;
    long int timestamp;
    std::string target;
    int nonce;
    std::string hash;
};