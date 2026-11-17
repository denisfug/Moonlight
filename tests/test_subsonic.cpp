// Music server (Subsonic/Navidrome) client: MD5 token auth, response parsing, errors, live server.
#include <cstdlib>

#include "core/md5.hpp"
#include "music/subsonic.hpp"
#include "platform/host/host_platform.hpp"
#include "player/player.hpp"
#include "test.hpp"

using namespace ml;
using namespace ml::subsonic;

TEST(md5_rfc1321_vectors)
{
    CHECK_EQ(md5Hex(""), std::string("d41d8cd98f00b204e9800998ecf8427e"));
    CHECK_EQ(md5Hex("abc"), std::string("900150983cd24fb0d6963f7d28e17f72"));
    CHECK_EQ(md5Hex("message digest"), std::string("f96b697d7cb7938d525a2f31aaf161d0"));
    CHECK_EQ(md5Hex("12345678901234567890123456789012345678901234567890123456789012345678901234567890"),
             std::string("57edf4a22be3c955ac49da2e2107b67a"));
    // example from the Subsonic API docs: md5("sesame" + "c19b2d")
    CHECK_EQ(md5Hex("sesamec19b2d"), std::string("26719a1196d2a940705a59634eb18eab"));
}

TEST(subsonic_config_and_urls)
{
    Config c = makeConfig("http://192.168.15.10:4533/", "joe", "sesame", 42);
    CHECK_EQ(c.url, std::string("http://192.168.15.10:4533"));
    CHECK_EQ(c.token, md5Hex("sesame" + c.salt));
    CHECK(c.salt.size() == 12);
    CHECK(c.configured());
    Client cl(nullptr, c);
    std::string u = cl.streamUrl("s 1");
    CHECK(u.find("/rest/stream.view?u=joe&t=" + c.token + "&s=" + c.salt) != std::string::npos);
    CHECK(u.find("&id=s%201&format=mp3&maxBitRate=192") != std::string::npos);
    CHECK(u.find("sesame") == std::string::npos);                 // password never appears
    CHECK(redactUrl(u).find(c.token) == std::string::npos);      // credentials hidden when logged
    std::string pong;
    CHECK(cl.ping().find("rede") != std::string::npos);           // no http client
    Client remote(nullptr, makeConfig("http://music.example.com", "joe", "x", 1));
    host::HostHttp http;
    Client remote2(&http, remote.config());
    CHECK(remote2.ping().find("HTTP sem criptografia") != std::string::npos);   // plain http only on LAN
    host::HostFileSystem fs(t::scratch);
    fs.makeDirs("/sub");
    JsonStore st(fs, "/sub");
    CHECK(saveConfig(st, c));
    Config back = loadConfig(st);
    CHECK(back.token == c.token && back.url == c.url && back.user == "joe");
    std::string raw;
    fs.readFile("/sub/subsonic.json", raw, 4096);
    CHECK(raw.find("sesame") == std::string::npos);
}

TEST(subsonic_parse_errors)
{
    Json p;
    CHECK(Client::parse("<html>", p).find("invalida") != std::string::npos);
    CHECK(Client::parse(R"({"other":1})", p).find("invalida") != std::string::npos);
    CHECK(Client::parse(R"({"subsonic-response":{"status":"failed","error":{"code":40,"message":"Wrong"}}})", p)
              .find("senha") != std::string::npos);
    CHECK(Client::parse(R"({"subsonic-response":{"status":"failed","error":{"code":70,"message":"Not found"}}})", p)
              .find("Not found") != std::string::npos);
    CHECK(Client::parse(R"({"subsonic-response":{"status":"ok","version":"1.16.1"}})", p).empty());
    auto songs = parseSongs(parseJson(R"([{"id":"a","title":"x","duration":10},{"id":"d","isDir":true,"title":"dir"},{"id":"","title":"y"}])"));
    CHECK_EQ(songs.size(), size_t(1));
    CHECK_EQ(songs[0].durationSec, 10);
}

TEST(subsonic_live_local_server)
{
    const char *base = std::getenv("ML_SUBSONIC_URL");   // set by tools/run_live_test.sh
    if (!base) { std::printf("(skipped: ML_SUBSONIC_URL not set) "); return; }
    host::HostHttp http;
    Client cl(&http, makeConfig(base, "joe", "sesame", 7));
    CHECK(cl.ping().empty());
    std::vector<Playlist> pls;
    CHECK(cl.playlists(pls).empty());
    CHECK(pls.size() == 2 && pls[1].id == "800000002");   // numeric ids accepted
    std::vector<Song> songs;
    CHECK(cl.playlist("pl1", songs).empty());
    CHECK_EQ(songs.size(), size_t(2));                      // video entry skipped
    CHECK(cl.randomSongs(10, songs).empty() && songs.size() == 1);
    CHECK(cl.search("tom", songs).empty() && songs.size() == 1);
    host::HostPlatform plat(t::scratch, "");
    AudioPlayer p(plat);
    std::string err;
    CHECK(p.openStream(cl.streamUrl("s1"), &http, &err, false, 3000));
    CHECK(!p.live() && p.remote());
    p.play();
    for (int i = 0; i < 600 && p.state() == PlayState::Playing; i++) plat.sleepMs(5);
    CHECK(p.state() == PlayState::Ended);   // finite song ends normally (queue can advance)
}
