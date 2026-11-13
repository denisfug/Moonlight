// Music library, ID3, queue and the MP3 audio player (host audio sink).
#include "music/music.hpp"
#include "platform/host/host_platform.hpp"
#include "player/player.hpp"
#include "test.hpp"

#include <cstdio>
#include <set>
#include <sys/stat.h>

using namespace ml;
using namespace ml::music;

static void copyFile(const std::string &from, const std::string &to)
{
    host::HostFileSystem fs("");
    std::string d;
    fs.readFile(from, d, 1 << 24);
    fs.writeFile(to, d);
}

TEST(music_id3_and_filenames)
{
    host::HostFileSystem fs("");
    std::string head;
    CHECK(fs.readFile(t::fixtures + "/tone.mp3", head, 65536));
    Id3Info id = parseId3(head);
    CHECK_EQ(id.title, std::string("Tom de Teste"));
    CHECK_EQ(id.artist, std::string("Moonlight HB"));
    CHECK_EQ(id.album, std::string("Fixtures"));
    CHECK(id.tagBytes > 10);
    CHECK_EQ(parseId3("not a tag").tagBytes, size_t(0));
    // UTF-16 TIT2 frame (ID3v2.3) with BOM
    std::string tag = std::string("ID3\x03\x00\x00\x00\x00\x00\x15", 10) + "TIT2" + std::string("\x00\x00\x00\x0B\x00\x00", 6) +
                      std::string("\x01\xFF\xFE" "C\x00\xE7\x00" "a\x00" "o\x00", 11);
    CHECK_EQ(parseId3(tag).title, std::string("C\xC3\xA7" "ao"));
    std::string t1, a1;
    titleFromFilename("012 - Artista X - Musica Y.mp3", t1, a1);
    CHECK_EQ(a1, std::string("Artista X"));
    CHECK_EQ(t1, std::string("Musica Y"));
    std::string t2, a2;
    titleFromFilename("Somente titulo.mp3", t2, a2);
    CHECK(t2 == "Somente titulo" && a2.empty());
}

TEST(music_scan_and_search)
{
    host::HostFileSystem fs(t::scratch);
    fs.makeDirs("/data/moonlight/music/Viagem");
    copyFile(t::fixtures + "/tone.mp3", t::scratch + "/data/moonlight/music/Viagem/01 tom.mp3");
    copyFile(t::fixtures + "/tone.mp3", t::scratch + "/data/moonlight/music/solto.MP3");
    fs.writeFile("/data/moonlight/music/Viagem/capa.jpg", "x");
    MusicLibrary lib;
    CHECK_EQ(lib.scan(fs, {"/data/moonlight/music", "/mnt/usb0/Music"}), size_t(2));
    CHECK_EQ(lib.playlists().size(), size_t(2));
    CHECK_EQ(lib.playlists()[0].name, std::string("Viagem"));
    CHECK_EQ(lib.playlists()[1].name, std::string("Soltas"));
    CHECK_EQ(lib.tracks()[0].title, std::string("Tom de Teste"));   // ID3 wins over filename
    lib.addDemo();
    CHECK(lib.playlists().size() >= 5);
    CHECK_EQ(lib.search("moonlight").size(), size_t(2));
    CHECK_EQ(lib.search("BRUMA").size(), size_t(2));
    CHECK(lib.search("  ").empty());
}

TEST(music_queue)
{
    Queue q;
    q.set({10, 11, 12, 13}, 1);
    CHECK_EQ(q.current(), size_t(11));
    CHECK(q.next(false));
    CHECK(q.next(false));
    CHECK_EQ(q.current(), size_t(13));
    q.setRepeat(Repeat::Off);
    CHECK(!q.next(false));   // end of queue
    CHECK(q.next(true));     // user skip wraps
    CHECK_EQ(q.current(), size_t(10));
    CHECK(q.previous());
    CHECK_EQ(q.current(), size_t(13));
    q.setRepeat(Repeat::One);
    CHECK(q.next(false));
    CHECK_EQ(q.current(), size_t(13));
    q.setShuffle(true, 1234);
    CHECK_EQ(q.current(), size_t(13));   // current track kept
    std::set<size_t> seen(q.order().begin(), q.order().end());
    CHECK_EQ(seen.size(), size_t(4));   // permutation
    q.setShuffle(false, 0);
    CHECK_EQ(q.current(), size_t(13));
    CHECK_EQ(q.order()[0], size_t(10));
}

TEST(audio_player_mp3)
{
    host::HostPlatform plat(t::scratch, "");
    plat.fs().makeDirs("/m");
    copyFile(t::fixtures + "/tone.mp3", t::scratch + "/m/tone.mp3");
    plat.audioDumpPath = t::scratch + "/out.raw";
    AudioPlayer p(plat);
    std::string err;
    CHECK(!p.open("/m/missing.mp3", &err));
    CHECK(p.state() == PlayState::Error);
    plat.fs().writeFile("/m/fake.mp3", "this is not audio at all");
    CHECK(!p.open("/m/fake.mp3", &err));
    CHECK(p.open("/m/tone.mp3", &err));
    CHECK(p.getDuration() > 2900 && p.getDuration() < 3200);
    p.setVolume(150);
    CHECK_EQ(p.volume(), 100);
    p.play();
    for (int i = 0; i < 300 && p.state() == PlayState::Playing && p.getPosition() < 1000; i++) plat.sleepMs(5);
    p.pause();
    plat.sleepMs(20);
    int64_t at = p.getPosition();
    plat.sleepMs(60);
    CHECK_EQ(p.getPosition(), at);   // paused: position frozen
    CHECK(p.state() == PlayState::Paused);
    p.seek(2500);
    p.play();
    for (int i = 0; i < 400 && p.state() == PlayState::Playing; i++) plat.sleepMs(5);
    CHECK(p.state() == PlayState::Ended);
    CHECK_EQ(p.getPosition(), p.getDuration());
    p.stop();
    // decoded audio is a 440 Hz tone, not silence
    host::HostFileSystem raw("");
    std::string pcm;
    raw.readFile(t::scratch + "/out.raw", pcm, 1 << 24);
    int peak = 0;
    for (size_t i = 0; i + 1 < pcm.size(); i += 2) {
        int v = static_cast<int16_t>(static_cast<uint8_t>(pcm[i]) | static_cast<uint8_t>(pcm[i + 1]) << 8);
        if (v < 0) v = -v;
        if (v > peak) peak = v;
    }
    CHECK(peak > 3000);
    VideoPlayer vp;
    CHECK(!vp.open("http://x/y.mp4", &err));
    CHECK(err.find("MEDIA_ENGINE") != std::string::npos);
}
