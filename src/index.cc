#include "index.h"

#include "parallel.h"

#include <fstream>

size_t guess_size(size_t records, size_t bytes, size_t file_size) {
  double percent_complete = (double)(bytes) / file_size;
  size_t total_records = records / percent_complete * 1.1;
  return total_records;
}

using namespace vroom;

// const char index::skip_lines() {
// auto start = mmap_.data();
// while (skip > 0) {
//--skip;
// start = strchr(start + 1, '\n');
//}

// return start;
//}

index::index(
    const char* filename,
    const char delim,
    const char quote,
    const bool trim_ws,
    const bool escape_double,
    const bool escape_backslash,
    const bool has_header,
    const size_t skip,
    size_t num_threads)
    : filename_(filename),
      has_header_(has_header),
      quote_(quote),
      trim_ws_(trim_ws),
      escape_double_(escape_double),
      escape_backslash_(escape_backslash),
      rows_(0),
      columns_(0) {

  std::error_code error;
  mmap_ = mio::make_mmap_source(filename, error);

  if (error) {
    throw Rcpp::exception(error.message().c_str(), false);
  }

  auto file_size = mmap_.cend() - mmap_.cbegin();

  // We read the values into a vector of vectors, then merge them afterwards
  std::vector<std::vector<size_t> > values(num_threads + 1);

  // Index the first row
  // TODO: skip leading blank lines / comments
  auto first_nl = find_next_newline(mmap_, 0);
  values[0].push_back(0);
  index_region(mmap_, values[0], delim, quote, 0, first_nl);
  columns_ = values[0].size();

  // This should be enough to ensure the first line fits in one thread, I
  // hope...
  if (file_size < 32768) {
    num_threads = 1;
  }

  parallel_for(
      file_size - first_nl,
      [&](int start, int end, int id) {
        values[id].reserve(128);
        start = find_next_newline(mmap_, first_nl + start);
        end = find_next_newline(mmap_, first_nl + end);
        // Rcpp::Rcerr << "Indexing start: ", v.size() << '\n';
        index_region(mmap_, values[id + 1], delim, quote, start, end);
      },
      num_threads,
      true);

  auto total_size = std::accumulate(
      values.begin(),
      values.end(),
      0,
      [](size_t sum, const std::vector<size_t>& v) {
        sum += v.size();
#if DEBUG
        Rcpp::Rcerr << v.size() << '\n';
#endif
        return sum;
      });

  idx_.reserve(total_size);

#if DEBUG
  Rcpp::Rcerr << "combining vectors of size: " << total_size << "\n";
#endif

  for (auto& v : values) {

    idx_.insert(
        std::end(idx_),
        std::make_move_iterator(std::begin(v)),
        std::make_move_iterator(std::end(v)));
  }

  rows_ = idx_.size() / columns_;

  if (has_header_) {
    --rows_;
  }

#if DEBUG
  std::ofstream log(
      "index.idx",
      std::fstream::out | std::fstream::binary | std::fstream::trunc);
  for (auto& v : idx_) {
    log << v << '\n';
  }
  log.close();
  Rcpp::Rcerr << columns_ << ':' << rows_ << '\n';
#endif
}

void index::trim_quotes(const char*& begin, const char*& end) const {
  if (begin != end && (*begin == quote_)) {
    ++begin;
  }

  if (end != begin && *(end - 1) == quote_) {
    --end;
  }
}

void index::trim_whitespace(const char*& begin, const char*& end) const {
  static const std::locale loc("");
  while (begin != end && std::isspace(*begin, loc)) {
    ++begin;
  }

  while (end != begin && std::isspace(*(end - 1), loc)) {
    --end;
  }
}

std::string
index::get_escaped_string(const char* begin, const char* end) const {
  std::string out;
  out.reserve(end - begin);

  while (begin < end) {
    if ((escape_double_ && *begin == '"') ||
        (escape_backslash_ && *begin == '\\')) {
      ++begin;
    }

    out.push_back(*begin++);
  }

  return out;
}

const std::string index::get_trimmed_val(size_t i) const {
  auto begin = mmap_.data() + idx_[i];
  auto end = mmap_.data() + idx_[i + 1] - 1;

  if (trim_ws_) {
    trim_whitespace(begin, end);
  }

  if (quote_ != '\0') {
    trim_quotes(begin, end);
  }

  std::string out;

  if (escape_double_ || escape_backslash_) {
    out = get_escaped_string(begin, end);
  } else {
    out = std::string(begin, end - begin);
  }

  return out;
}

const std::string index::get(size_t row, size_t col) const {
  auto i = (row + has_header_) * columns_ + col;

  return get_trimmed_val(i);
}