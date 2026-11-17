#include "app/demo_data.hpp"

#include <algorithm>

namespace ml::demo {

namespace {

struct M {
    const char *id, *name, *year, *rating, *runtime, *genre, *desc;
};

const M kMovies[] = {
    {"demo:m1", "O Farol de Vidro", "2024", "7.6", "104 min", "Drama",
     "Numa ilha sem sinal, a faroleira Iara percebe que a luz do farol responde a mensagens que ninguem enviou."},
    {"demo:m2", "Orbita Baixa", "2023", "7.1", "118 min", "Ficcao",
     "A ultima tripulacao de uma estacao orbital decide se volta para casa ou espera um sinal de resgate."},
    {"demo:m3", "Cidade das Antenas", "2022", "6.8", "97 min", "Suspense",
     "Um tecnico de radio descobre que todas as antenas da cidade apontam para o mesmo predio abandonado."},
    {"demo:m4", "Mares de Neon", "2025", "7.9", "126 min", "Ficcao",
     "Em 2089, uma piloto de drones submarinos encontra uma cidade inteira sob a baia."},
    {"demo:m5", "A Ultima Sessao", "2021", "7.3", "92 min", "Drama",
     "O projecionista de um cinema de rua prepara a sessao de despedida para uma plateia de uma pessoa so."},
    {"demo:m6", "Riso de Domingo", "2024", "6.5", "88 min", "Comedia",
     "Tres irmaos tentam reabrir o restaurante da familia em um unico fim de semana."},
    {"demo:m7", "Sombras no Cerrado", "2023", "7.0", "110 min", "Suspense",
     "Uma biologa investiga luzes que aparecem toda noite no mesmo trecho da chapada."},
    {"demo:m8", "Pequeno Cometa", "2022", "8.1", "84 min", "Animacao",
     "Um cometa jovem foge da propria orbita para conhecer a Terra antes de desaparecer."},
    {"demo:m9", "Correnteza", "2025", "6.9", "101 min", "Acao",
     "Uma equipe de resgate fluvial tem uma noite para atravessar a maior enchente em decadas."},
    {"demo:m10", "Arquivo Morto", "2020", "7.4", "115 min", "Suspense",
     "Uma arquivista encontra o proprio nome em um processo de 1958."},
    {"demo:m11", "Ventos de Agosto", "2021", "6.7", "96 min", "Drama",
     "Durante a colheita, duas amigas de infancia reencontram o que deixaram para tras."},
    {"demo:m12", "Planeta Quintal", "2024", "7.8", "79 min", "Animacao",
     "Formigas cartografas mapeiam um quintal como se fosse um continente inteiro."},
};

struct S {
    const char *id, *name, *year, *rating, *genre, *desc;
    int seasons, episodes;
};

const S kSeries[] = {
    {"demo:s1", "Cidade Submersa", "2023-", "8.2", "Ficcao", "Uma cidade costeira acorda um metro abaixo do nivel do mar - e continua afundando.", 2, 6},
    {"demo:s2", "Plantao Noturno", "2022-", "7.7", "Drama", "O dia a dia de um hospital que so funciona entre meia-noite e seis da manha.", 3, 5},
    {"demo:s3", "Codigo Morse", "2024-", "7.5", "Suspense", "Uma radioamadora decifra mensagens enviadas de um navio que afundou em 1971.", 1, 8},
    {"demo:s4", "Estudio 9", "2021-", "7.1", "Comedia", "Os bastidores de um programa de auditorio que nunca foi ao ar.", 2, 7},
    {"demo:s5", "Fronteira Azul", "2025-", "8.0", "Acao", "Guardas-parque protegem a ultima reserva marinha do Atlantico Sul.", 1, 6},
    {"demo:s6", "Nuvem de Papel", "2023-", "7.9", "Animacao", "Uma menina e um dragao de origami entregam cartas em uma cidade que esqueceu como escrever.", 2, 10},
};

const char *kEpisodeWords[] = {"Piloto", "Mare Alta", "Comportas", "Frequencia", "Silencio", "Correnteza",
                               "Sinal Fraco", "Lua Nova", "Arquivo", "Despedida"};

}  // namespace

std::vector<addon::MetaPreview> movies()
{
    std::vector<addon::MetaPreview> v;
    for (const auto &m : kMovies) {
        addon::MetaPreview p;
        p.id = m.id; p.type = "movie"; p.name = m.name; p.releaseInfo = m.year; p.imdbRating = m.rating;
        p.description = m.desc; p.genres = {m.genre};
        v.push_back(p);
    }
    return v;
}

std::vector<addon::MetaPreview> series()
{
    std::vector<addon::MetaPreview> v;
    for (const auto &s : kSeries) {
        addon::MetaPreview p;
        p.id = s.id; p.type = "series"; p.name = s.name; p.releaseInfo = s.year; p.imdbRating = s.rating;
        p.description = s.desc; p.genres = {s.genre};
        v.push_back(p);
    }
    return v;
}

bool meta(const std::string &id, addon::MetaDetail &out)
{
    for (const auto &m : kMovies) {
        if (id != m.id) continue;
        out = addon::MetaDetail();
        out.id = m.id; out.type = "movie"; out.name = m.name; out.releaseInfo = m.year; out.imdbRating = m.rating;
        out.runtime = m.runtime; out.genres = {m.genre}; out.description = m.desc;
        out.cast = {"Elenco de demonstracao"};
        return true;
    }
    for (const auto &s : kSeries) {
        if (id != s.id) continue;
        out = addon::MetaDetail();
        out.id = s.id; out.type = "series"; out.name = s.name; out.releaseInfo = s.year; out.imdbRating = s.rating;
        out.genres = {s.genre}; out.description = s.desc;
        for (int se = 1; se <= s.seasons; se++)
            for (int ep = 1; ep <= s.episodes; ep++) {
                addon::Video v;
                v.id = std::string(s.id) + "_s" + std::to_string(se) + "e" + std::to_string(ep);
                v.season = se;
                v.episode = ep;
                v.title = kEpisodeWords[(se * 3 + ep) % 10];
                v.overview = "Episodio " + std::to_string(ep) + " da temporada " + std::to_string(se) + " (demonstracao).";
                out.videos.push_back(v);
            }
        return true;
    }
    return false;
}

std::vector<Channel> channels()
{
    const char *n[][2] = {{"Moonlight News", "Noticias"}, {"Canal Orbita", "Noticias"}, {"Radar 24h", "Noticias"},
                          {"Clipes Noturnos", "Musica"}, {"Lo-fi Lua", "Musica"}, {"Arena Esportes", "Esportes"},
                          {"Mar Aberto", "Documentarios"}, {"Planeta Vivo", "Documentarios"}, {"Infantil Cometa", "Infantil"}};
    std::vector<Channel> v;
    for (auto &c : n) {
        Channel ch;
        ch.name = c[0];
        ch.group = c[1];
        ch.tvgId = std::string("demo:") + c[0];
        v.push_back(ch);   // no URL: demonstration only
    }
    return v;
}

std::vector<std::string> genres(const std::string &type)
{
    std::vector<std::string> g{"Todos"};
    auto add = [&](const std::vector<addon::MetaPreview> &v) {
        for (const auto &m : v)
            for (const auto &x : m.genres)
                if (std::find(g.begin(), g.end(), x) == g.end()) g.push_back(x);
    };
    add(type == "series" ? series() : movies());
    return g;
}

}  // namespace ml::demo
