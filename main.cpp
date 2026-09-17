#include "libutils/LoadingBar.hpp"
#include "libutils/funcs.hpp"
#include "libutils/numutils.hpp"
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

// #include <liburing.h>

namespace fs = std::filesystem;
using namespace std::chrono;

// 1: repeated / sequential digit patterns
std::vector<std::string> repeatedAndSequential() {
  std::vector<std::string> out;

  // All-same: 00000000, 11111111
  for (int d = 0; d <= 9; ++d)
    out.push_back(std::string(8, static_cast<char>('0' + d)));

  // 12345678, 23456789, 34567890,  90123456
  for (int start = 0; start <= 9; ++start) {
    std::string s;
    for (int i = 0; i < 8; ++i)
      s += static_cast<char>('0' + (start + i) % 10);
    out.push_back(s);
  }
  // Descending: 87654321, 76543210
  for (int start = 0; start <= 9; ++start) {
    std::string s;
    for (int i = 0; i < 8; ++i)
      s += static_cast<char>('0' + (start - i + 100) % 10);
    out.push_back(s);
  }

  // Repeating short blocks
  for (int a = 0; a <= 9; ++a) {
    for (int b = 0; b <= 9; ++b) {
      if (a == b)
        continue; // already covered by all-same
      std::string block = {static_cast<char>('0' + a),
                           static_cast<char>('0' + b)};
      std::string s;
      for (int i = 0; i < 4; ++i)
        s += block; // "ab" * 4 = 8 chars
      out.push_back(s);
    }
  }
  for (int a = 0; a <= 9; ++a) {
    for (int b = 0; b <= 9; ++b) {
      for (int c = 0; c <= 9; ++c) {
        for (int e = 0; e <= 9; ++e) {
          if (a == b && b == c && c == e)
            continue; // covered
          std::string block = {
              static_cast<char>('0' + a), static_cast<char>('0' + b),
              static_cast<char>('0' + c), static_cast<char>('0' + e)};
          out.push_back(block + block); // "abce" * 2 = 8 chars
        }
      }
    }
  }
  return out;
}

// 2: real calendar dates, 3 encodings
std::vector<std::string> calendarDates(int yearFrom, int yearTo) {
  std::vector<std::string> out;
  char buf[16];

  for (int y = yearFrom; y <= yearTo; ++y) {
    for (unsigned m = 1; m <= 12; ++m) {
      year_month yy_mm{year{y}, month{m}};

      auto lastDay = (yy_mm / last).day();
      for (unsigned d = 1; d <= static_cast<unsigned>(lastDay); ++d) {
        year_month_day ymd{year{y}, month{m}, day{d}};
        if (!ymd.ok())
          continue; // scary...should never trigger

        std::snprintf(buf, sizeof(buf), "%02u%02u%04d", d, m, y);
        out.push_back(buf); // DDMMYYYY
        std::snprintf(buf, sizeof(buf), "%02u%02u%04d", m, d, y);
        out.push_back(buf); // MMDDYYYY
        std::snprintf(buf, sizeof(buf), "%04d%02u%02u", y, m, d);
        out.push_back(buf); // YYYYMMDD
      }
    }
  }
  return out;
}

// 3: two "repeating halves" stitched together — aaaa|bbbb and cdcd|efef style
std::vector<std::string> twinHalves() {
  // every 4-char half built from a repeating sub-block: "aaaa" or "cdcd"
  std::vector<std::string> halves;
  for (int a = 0; a <= 9; ++a)
    halves.push_back(std::string(4, static_cast<char>('0' + a))); // aaaa

  for (int c = 0; c <= 9; ++c)
    for (int d = 0; d <= 9; ++d) {
      if (c == d)
        continue; // that's just "aaaa" in disguise
      std::string block = {static_cast<char>('0' + c),
                           static_cast<char>('0' + d)};
      halves.push_back(block + block); // cdcd
    }

  // stitch any two halves together — this is where 44448888 and 30309090 live
  std::vector<std::string> out;
  out.reserve(halves.size() * halves.size());
  for (auto &h1 : halves)
    for (auto &h2 : halves)
      out.push_back(h1 + h2);
  return out;
}

std::vector<std::string> bruteRule() {
  std::vector<std::string> results;

  std::string charset = "@#$&";
  for (const auto &st : charset) {
    for (const auto &nd : charset) {
      std::string st_half = std::string(4, st);
      std::string nd_half = std::string(4, nd);
      std::string password = st_half + nd_half;
      results.push_back(password);
    }
  }

  return results;
}

int main(int argc, char **argv) {
  // auto lines = File::numlines(outfile);
  //	std::cout << lines << " passwords found.\n";
  //	return 0;

  int yearFrom = 1950, yearTo = 2026;
  if (argc == 3) {
    yearFrom = std::atoi(argv[1]);
    yearTo = std::atoi(argv[2]);
  }

  Loadingbar::Spinner spinner_generating{
      {"▁", "▂", "▃", "▄", "▅", "▆", "▇", "█", "▇", "▆", "▅", "▄", "▃", "▂"},
      100,
      "Generating keys"};
  std::unordered_set<std::string> seen;
  std::vector<std::string> final;
  final.reserve(200000);

  auto addAll = [&](std::vector<std::string> &&v) {
    for (auto &s : v)
      if (seen.insert(s).second)
        final.push_back(std::move(s));
  };

  addAll(repeatedAndSequential()); // highest-probability
  addAll(twinHalves());            // <-- new
  addAll(calendarDates(yearFrom, yearTo));
  addAll(bruteRule());

  spinner_generating.stop();

  Loadingbar::StatusLine statusline_writing{1};
  auto createMsg = [](size_t current, const std::string &total) -> std::string {
    return "Writing " + std::to_string(current) + "/" + total;
  };
  size_t current = 0;
  std::string total = std::to_string(final.size());
  statusline_writing.setMsg(createMsg(current, total));

  std::string filename = "wordlist_" + numutils::human(round(final.size())) + ".txt";
  fs::path outfile = fs::path("out") / filename;

  std::ofstream file(outfile, std::ios::binary);
  size_t data_size = 0;
  for (const auto &l : final)
    data_size += l.size();
  std::string buf;
  buf.reserve(data_size);

  for (auto &s : final) {

    current++;
    statusline_writing.setMsg(createMsg(current, total));
    buf += s;
    buf += '\n';
  }
  file.write(buf.data(), buf.size());
  statusline_writing.stop();
  std::cout << createMsg(current, total);
  std::cout << std::endl
            << "Total: " << numutils::groupDigits(final.size()) << "\n";
}