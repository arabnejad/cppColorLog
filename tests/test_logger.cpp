#include "cppColorLogger/logger.h"

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <gtest/gtest.h>
#include <iomanip>
#include <memory>
#include <new>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

bool colorsAreAvailableFromAnotherTranslationUnit();
bool errorMacroRemainsDefinedAfterIncludingLogger();
bool infoIsEnabledFromAnotherTranslationUnit();

// These names and messages intentionally match the quick-start documentation.
// Keeping them outside the anonymous namespace makes the expected context
// exactly `refreshCache` and `Service::start` on supported compilers.
void refreshCache() {
  LOGGER_LOG(LogLevel::Info, "Cache refreshed");
}

void logCompletedRequestWithFields() {
  LOGGER_LOG_FIELDS(LogLevel::Info, "Request completed", {{"status", "200"}, {"duration_ms", "14"}});
}

class Service {
public:
  void start() {
    LOGGER_LOG(LogLevel::Info, "Service started");
  }
};

namespace cppcolorlogger_detail {

// Logger keeps construction and direct logging private in production. This
// test-only friend provides isolated Logger instances without changing the
// definitions seen by other translation units.
class LoggerTestAccess : public ::Logger {
public:
  explicit LoggerTestAccess(bool addDefaultConsoleSink = true) : Logger(addDefaultConsoleSink) {}

  using Logger::log;
};

} // namespace cppcolorlogger_detail

namespace {

// Production code uses LOGGER and the LOGGER_LOG macros. Tests use this
// internal type to give each case an isolated logger state.
using Logger = cppcolorlogger_detail::LoggerTestAccess;

class LoggerTest : public ::testing::Test {
protected:
  const std::string               logFile = "test_log_output.txt";
  std::stringstream               capturedCout;
  std::stringstream               capturedCerr;
  std::streambuf                 *oldCout = nullptr;
  std::streambuf                 *oldCerr = nullptr;
  std::unique_ptr<ScopedSettings> m_testSettings;

  void SetUp() override {
    removeTestLogFiles();
    m_testSettings.reset(new ScopedSettings(LOGGER));
    LOGGER.setLogLevel(LogLevel::Info);
    LOGGER.clearAllowedLevels();
    oldCout = std::cout.rdbuf(capturedCout.rdbuf());
    oldCerr = std::cerr.rdbuf(capturedCerr.rdbuf());
  }

  void TearDown() override {
    std::cout.rdbuf(oldCout);
    std::cerr.rdbuf(oldCerr);
    m_testSettings.reset();
    removeTestLogFiles();
  }

  std::string readFile(const std::string &filename) const {
    std::ifstream     file(filename.c_str(), std::ios::binary);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
  }

  std::string readFile() const {
    return readFile(logFile);
  }

  std::string backupFilename(std::size_t backupNumber) const {
    std::ostringstream filename;
    filename << logFile << '.' << backupNumber;
    return filename.str();
  }

  bool fileExists(const std::string &filename) const {
    std::ifstream file(filename.c_str(), std::ios::binary);
    return file.good();
  }

  void removeTestLogFiles() {
    std::remove(logFile.c_str());
    for (std::size_t index = 1; index <= 4; ++index)
      std::remove(backupFilename(index).c_str());
  }
};

class RecordingSink : public LogSink {
public:
  void write(const LogEntry &entry, const LogStyle &) override {
    messages.push_back(entry.formattedText);
  }

  std::vector<std::string> messages;
};

struct BlockingSinkState {
  std::mutex              write_mux;
  std::condition_variable write_cv;
  bool                    writeStarted = false;
  bool                    mayFinish    = false;
};

class BlockingSink : public LogSink {
public:
  explicit BlockingSink(const std::shared_ptr<BlockingSinkState> &state) : m_state(state) {}

  void write(const LogEntry &, const LogStyle &) override {
    std::unique_lock<std::mutex> write_lck(m_state->write_mux);
    m_state->writeStarted = true;
    m_state->write_cv.notify_all();
    m_state->write_cv.wait(write_lck, [this] { return m_state->mayFinish; });
  }

private:
  std::shared_ptr<BlockingSinkState> m_state;
};

class StructuredSink : public LogSink {
public:
  void write(const LogEntry &entry, const LogStyle &style) override {
    level      = entry.level;
    color      = style.color;
    colorMode  = style.colorMode;
    timestamp  = entry.timestamp;
    eventTime  = entry.eventTime;
    message    = entry.message;
    context    = entry.context;
    sourceFile = entry.sourceFile;
    sourceLine = entry.sourceLine;
    threadId   = entry.threadId;
    fields     = entry.fields;
    text       = entry.formattedText;
  }

  LogLevel                              level = LogLevel::Always;
  std::string                           color;
  ColorMode                             colorMode = ColorMode::Automatic;
  std::string                           timestamp;
  std::chrono::system_clock::time_point eventTime;
  std::string                           message;
  std::string                           context;
  std::string                           sourceFile;
  unsigned                              sourceLine = 0;
  std::string                           threadId;
  LogFields                             fields;
  std::string                           text;
};

class CompactLogFormatter : public LogFormatter {
public:
  std::string formatTimestamp(const std::chrono::system_clock::time_point &eventTime) const override {
    return formatTimeWithPattern(eventTime, "%H:%M:%S");
  }

  std::string format(const LogEntry &entry) const override {
    std::ostringstream output;
    output << entry.timestamp << ' ' << (entry.level == LogLevel::Info ? "I" : logLevelToString(entry.level)) << ' '
           << entry.context << " | " << entry.message;

    if (!entry.fields.empty()) {
      output << " {";
      for (LogFields::const_iterator field = entry.fields.begin(); field != entry.fields.end(); ++field) {
        if (field != entry.fields.begin())
          output << ' ';
        output << field->first << ':' << field->second;
      }
      output << '}';
    }
    return output.str();
  }
};

class MillisecondLogFormatter : public DefaultLogFormatter {
public:
  std::string formatTimestamp(const std::chrono::system_clock::time_point &eventTime) const override {
    std::ostringstream output;
    output << formatUtcTimeWithPattern(eventTime, "%Y-%m-%dT%H:%M:%S") << '.' << std::setfill('0') << std::setw(3)
           << millisecondsWithinSecond(eventTime) << 'Z';
    return output.str();
  }
};

class CountingLogFormatter : public LogFormatter {
public:
  CountingLogFormatter() : m_formatCount(0) {}

  std::string format(const LogEntry &entry) const override {
    ++m_formatCount;
    return entry.message;
  }

  int getFormatCount() const {
    return m_formatCount;
  }

private:
  mutable int m_formatCount;
};

class ReentrantSink : public LogSink {
public:
  explicit ReentrantSink(Logger &logger) : m_logger(logger) {}

  void write(const LogEntry &, const LogStyle &) override {
    ++messageCount;
    if (messageCount == 1)
      m_logger.log(LogLevel::Info, "Nested message", "ReentrantSink");
  }

  int messageCount = 0;

private:
  Logger &m_logger;
};

class ThrowingSink : public LogSink {
public:
  void write(const LogEntry &, const LogStyle &) override {
    throw std::runtime_error("write failed");
  }

  bool flush() override {
    throw std::runtime_error("flush failed");
  }
};

class ThrowingFormatter : public LogFormatter {
public:
  std::string format(const LogEntry &) const override {
    throw std::runtime_error("format failed");
  }
};

class RecursiveSink : public LogSink {
public:
  explicit RecursiveSink(Logger &logger) : m_logger(logger), writeCount(0) {}

  void write(const LogEntry &, const LogStyle &) override {
    ++writeCount;
    m_logger.log(LogLevel::Info, "Recursive message", "RecursiveSink");
  }

  Logger &m_logger;
  int     writeCount;
};

struct StreamCountingMessage {
  explicit StreamCountingMessage(int &count) : streamCount(count) {}

  int &streamCount;
};

std::ostream &operator<<(std::ostream &stream, const StreamCountingMessage &message) {
  ++message.streamCount;
  return stream << "counted message";
}

std::string buildCountedMessage(int &buildCount) {
  ++buildCount;
  return "expensive debug message";
}

LogEntry makeFormattedEntry(const std::string &text) {
  LogEntry entry      = {};
  entry.formattedText = text;
  return entry;
}

class ContextExample {
public:
  void emit() {
    LOGGER_LOG(LogLevel::Info, "Class context");
  }
};

void logFromAutomaticFreeFunction() {
  LOGGER_LOG(LogLevel::Info, "Automatic free function");
}

template <typename T> void logFromAutomaticFunctionTemplate(const T &value) {
  LOGGER_LOG(LogLevel::Info, value);
}

class AutomaticContextExample {
public:
  AutomaticContextExample() {
    LOGGER_LOG(LogLevel::Info, "Automatic constructor");
  }

  ~AutomaticContextExample() {
    LOGGER_LOG(LogLevel::Info, "Automatic destructor");
  }

  void member() {
    LOGGER_LOG(LogLevel::Info, "Automatic member");
  }

  static void staticMember() {
    LOGGER_LOG(LogLevel::Info, "Automatic static member");
  }

  template <typename T> void write(const T &value) {
    LOGGER_LOG(LogLevel::Info, value);
  }

  void operator()() {
    LOGGER_LOG(LogLevel::Info, "Automatic operator");
  }
};

template <typename T> class AutomaticRepository {
public:
  void save(const T &) {
    LOGGER_LOG(LogLevel::Info, "Automatic class template");
  }
};

TEST_F(LoggerTest, LogsToFileWithoutColor) {
  const std::shared_ptr<FileSink> sink = LOGGER.addFileSink(logFile);
  ASSERT_TRUE(sink->isOpen());
  LOGGER.setColorMode(ColorMode::Enabled);

  LOGGER_LOG(LogLevel::Info, "File log test");

  const std::string content = readFile();
  EXPECT_NE(content.find("File log test"), std::string::npos);
  EXPECT_EQ(content.find("\033["), std::string::npos);
}

TEST_F(LoggerTest, FileSinkAppendModePreservesExistingContent) {
  {
    std::ofstream existingFile(logFile.c_str(), std::ios::trunc);
    existingFile << "Existing content\n";
  }

  FileSink sink(logFile, FileOpenMode::Append);
  ASSERT_TRUE(sink.isOpen());
  sink.write(makeFormattedEntry("Appended content"), LogStyle());
  ASSERT_TRUE(sink.flush());

  const std::string content = readFile();
  EXPECT_NE(content.find("Existing content"), std::string::npos);
  EXPECT_NE(content.find("Appended content"), std::string::npos);
  EXPECT_FALSE(sink.hasError());
}

TEST_F(LoggerTest, FileSinkTruncateModeClearsExistingContent) {
  {
    std::ofstream existingFile(logFile.c_str(), std::ios::trunc);
    existingFile << "Content to remove\n";
  }

  FileSink sink(logFile, FileOpenMode::Truncate);
  ASSERT_TRUE(sink.isOpen());
  sink.write(makeFormattedEntry("Replacement content"), LogStyle());
  ASSERT_TRUE(sink.flush());

  const std::string content = readFile();
  EXPECT_EQ(content.find("Content to remove"), std::string::npos);
  EXPECT_NE(content.find("Replacement content"), std::string::npos);
}

TEST_F(LoggerTest, DefaultFileFlushModeMakesAnEntryImmediatelyReadable) {
  FileSink sink(logFile, FileOpenMode::Truncate);
  ASSERT_TRUE(sink.isOpen());

  sink.write(makeFormattedEntry("Immediately flushed"), LogStyle());

  EXPECT_EQ(readFile(), "Immediately flushed\n");
  EXPECT_FALSE(sink.hasError());
}

TEST_F(LoggerTest, ManualFileFlushModePreservesCompleteEntries) {
  Logger                          logger(false);
  const std::shared_ptr<FileSink> sink = logger.addFileSink(logFile, FileOpenMode::Truncate, FileFlushMode::Manual);
  ASSERT_TRUE(sink->isOpen());

  logger.log(LogLevel::Info, "First buffered entry", "manualFlushTest");
  logger.log(LogLevel::Info, "Second buffered entry", "manualFlushTest");
  ASSERT_TRUE(logger.flushAllSinks());

  const std::string content = readFile();
  EXPECT_NE(content.find("First buffered entry\n"), std::string::npos);
  EXPECT_NE(content.find("Second buffered entry\n"), std::string::npos);
  EXPECT_FALSE(sink->hasError());
}

TEST_F(LoggerTest, ClosingManualFileSinkWritesBufferedEntries) {
  {
    FileSink sink(logFile, FileOpenMode::Truncate, FileFlushMode::Manual);
    ASSERT_TRUE(sink.isOpen());
    sink.write(makeFormattedEntry("Flushed when closed"), LogStyle());
  }

  EXPECT_EQ(readFile(), "Flushed when closed\n");
}

TEST_F(LoggerTest, FlushAllSinksWritesAllActiveFileSinks) {
  Logger                          logger(false);
  const std::shared_ptr<FileSink> sink = logger.addFileSink(logFile, FileOpenMode::Truncate);
  ASSERT_TRUE(sink->isOpen());

  logger.log(LogLevel::Info, "Flushed through logger", "flushTest");

  EXPECT_TRUE(logger.flushAllSinks());
  EXPECT_FALSE(sink->hasError());
  EXPECT_NE(readFile().find("Flushed through logger"), std::string::npos);
}

TEST_F(LoggerTest, FileSinkReportsOpenFailureAndKeepsFirstError) {
  const std::string invalidPath = logFile + "/cannot-open.log";
  FileSink          sink(invalidPath, FileOpenMode::Append);

  EXPECT_FALSE(sink.isOpen());
  ASSERT_TRUE(sink.hasError());
  const std::string openError = sink.getLastError();
  EXPECT_NE(openError.find("Failed to open"), std::string::npos);
  EXPECT_NE(openError.find(invalidPath), std::string::npos);

  sink.write(makeFormattedEntry("Ignored after open failure"), LogStyle());
  EXPECT_FALSE(sink.flush());
  EXPECT_EQ(sink.getLastError(), openError);
}

TEST_F(LoggerTest, RotatingFileSinkMovesCompleteEntriesBeforeWritingTheNextOne) {
  RotatingFileSink sink(logFile, 6, 2);
  ASSERT_TRUE(sink.isOpen());

  sink.write(makeFormattedEntry("one"), LogStyle());
  sink.write(makeFormattedEntry("two"), LogStyle());

  EXPECT_EQ(readFile(), "two\n");
  EXPECT_EQ(readFile(backupFilename(1)), "one\n");
  EXPECT_FALSE(fileExists(backupFilename(2)));
  EXPECT_FALSE(sink.hasError());
}

TEST_F(LoggerTest, RotatingFileSinkKeepsOnlyTheConfiguredNumberOfBackups) {
  RotatingFileSink sink(logFile, 6, 2);
  ASSERT_TRUE(sink.isOpen());

  sink.write(makeFormattedEntry("one"), LogStyle());
  sink.write(makeFormattedEntry("two"), LogStyle());
  sink.write(makeFormattedEntry("three"), LogStyle());
  sink.write(makeFormattedEntry("four"), LogStyle());

  EXPECT_EQ(readFile(), "four\n");
  EXPECT_EQ(readFile(backupFilename(1)), "three\n");
  EXPECT_EQ(readFile(backupFilename(2)), "two\n");
  EXPECT_FALSE(fileExists(backupFilename(3)));
  EXPECT_FALSE(sink.hasError());
}

TEST_F(LoggerTest, RotatingFileSinkIncludesExistingContentInItsSizeCheck) {
  {
    std::ofstream existingFile(logFile.c_str(), std::ios::binary | std::ios::trunc);
    existingFile << "old\n";
  }

  RotatingFileSink sink(logFile, 6, 1);
  ASSERT_TRUE(sink.isOpen());
  sink.write(makeFormattedEntry("new"), LogStyle());

  EXPECT_EQ(readFile(), "new\n");
  EXPECT_EQ(readFile(backupFilename(1)), "old\n");
}

TEST_F(LoggerTest, RotatingFileSinkNeverSplitsAnOversizedEntry) {
  RotatingFileSink sink(logFile, 5, 1);
  ASSERT_TRUE(sink.isOpen());

  sink.write(makeFormattedEntry("oversized"), LogStyle());
  EXPECT_EQ(readFile(), "oversized\n");
  EXPECT_FALSE(fileExists(backupFilename(1)));

  sink.write(makeFormattedEntry("next"), LogStyle());
  EXPECT_EQ(readFile(), "next\n");
  EXPECT_EQ(readFile(backupFilename(1)), "oversized\n");
}

TEST_F(LoggerTest, RotatingFileSinkCanDiscardOldContentWithoutKeepingBackups) {
  RotatingFileSink sink(logFile, 6, 0);
  ASSERT_TRUE(sink.isOpen());

  sink.write(makeFormattedEntry("one"), LogStyle());
  sink.write(makeFormattedEntry("two"), LogStyle());

  EXPECT_EQ(readFile(), "two\n");
  EXPECT_FALSE(fileExists(backupFilename(1)));
}

TEST_F(LoggerTest, RotatingFileSinkSupportsManualFlushing) {
  Logger                                  logger(false);
  const std::shared_ptr<RotatingFileSink> sink = logger.addRotatingFileSink(logFile, 12, 1, FileFlushMode::Manual);
  ASSERT_TRUE(sink->isOpen());

  logger.log(LogLevel::Info, "Buffered rotating entry", "rotationTest");
  ASSERT_TRUE(logger.flushAllSinks());

  EXPECT_NE(readFile().find("Buffered rotating entry"), std::string::npos);
  EXPECT_FALSE(sink->hasError());
}

TEST_F(LoggerTest, RotatingFileSinkRejectsAZeroSizeLimit) {
  RotatingFileSink sink(logFile, 0, 1);

  EXPECT_FALSE(sink.isOpen());
  EXPECT_TRUE(sink.hasError());
  EXPECT_NE(sink.getLastError().find("greater than zero"), std::string::npos);
}

TEST_F(LoggerTest, RotatingFileSinkReportsOpenFailure) {
  const std::string invalidPath = logFile + "/cannot-open.log";
  RotatingFileSink  sink(invalidPath, 100, 1);

  EXPECT_FALSE(sink.isOpen());
  EXPECT_TRUE(sink.hasError());
  EXPECT_NE(sink.getLastError().find("Failed to open"), std::string::npos);
}

#if defined(__linux__)
TEST(FileSinkFailureTest, ReportsFailureWhenFileStopsAcceptingWrites) {
  Logger                          logger(false);
  const std::shared_ptr<FileSink> sink = logger.addFileSink("/dev/full");
  ASSERT_TRUE(sink->isOpen());

  logger.log(LogLevel::Info, "This write cannot complete", "failureTest");

  EXPECT_TRUE(sink->hasError());
  EXPECT_NE(sink->getLastError().find("Failed to write"), std::string::npos);
  EXPECT_FALSE(logger.flushAllSinks());
}
#endif

TEST_F(LoggerTest, LogLevelThresholdWorks) {
  LOGGER.addFileSink(logFile);
  LOGGER.setLogLevel(LogLevel::Error);

  LOGGER_LOG(LogLevel::Debug, "This should not appear");
  LOGGER_LOG(LogLevel::Error, "This should appear");

  const std::string content = readFile();
  EXPECT_EQ(content.find("This should not appear"), std::string::npos);
  EXPECT_NE(content.find("This should appear"), std::string::npos);
}

TEST_F(LoggerTest, AllowedLevelListWorks) {
  LOGGER.addFileSink(logFile);
  LOGGER.setLogLevel(LogLevel::Debug);
  LOGGER.setAllowedLevels({LogLevel::Error});

  LOGGER_LOG(LogLevel::Debug, "Filtered out");
  LOGGER_LOG(LogLevel::Error, "Filtered in");

  const std::string content = readFile();
  EXPECT_EQ(content.find("Filtered out"), std::string::npos);
  EXPECT_NE(content.find("Filtered in"), std::string::npos);
}

TEST_F(LoggerTest, ScopedSettingsRestoresConfiguration) {
  LOGGER.addFileSink(logFile);
  LOGGER.setLogLevel(LogLevel::Info);
  {
    ScopedSettings temporary = LOGGER.scopedSettings();
    LOGGER.setLogLevel(LogLevel::Error);
    LOGGER_LOG(LogLevel::Info, "Hidden in scope");
  }
  LOGGER_LOG(LogLevel::Info, "Visible after scope");

  const std::string content = readFile();
  EXPECT_EQ(content.find("Hidden in scope"), std::string::npos);
  EXPECT_NE(content.find("Visible after scope"), std::string::npos);
}

TEST_F(LoggerTest, InMemorySinkCapturesLog) {
  LOGGER.enableInMemorySink();
  LOGGER.setColorMode(ColorMode::Enabled);
  LOGGER_LOG(LogLevel::Info, "Memory captured log");

  const std::vector<std::string> logs = LOGGER.getInMemoryLogs();
  ASSERT_FALSE(logs.empty());
  EXPECT_NE(logs.back().find("Memory captured log"), std::string::npos);
  EXPECT_EQ(logs.back().find("\033["), std::string::npos);
}

TEST_F(LoggerTest, InMemorySinkCanBeEnabledAgainAfterScopedSettingsEnds) {
  {
    ScopedSettings temporary = LOGGER.scopedSettings();
    LOGGER.enableInMemorySink();
  }

  const std::shared_ptr<InMemorySink> sink = LOGGER.enableInMemorySink();
  LOGGER_LOG(LogLevel::Info, "Captured after pop");

  ASSERT_TRUE(sink);
  ASSERT_FALSE(sink->getLogs().empty());
}

TEST_F(LoggerTest, ConsoleUsesConfiguredColor) {
  std::string customColor = Color::Cyan;
  LOGGER.setLevelColor(LogLevel::Info, customColor);
  LOGGER.setColorMode(ColorMode::Enabled);
  customColor.clear();
  LOGGER_LOG(LogLevel::Info, "Custom color");

  const std::string output = capturedCout.str();
  EXPECT_NE(output.find(Color::Cyan), std::string::npos);
  EXPECT_NE(output.find(Color::Reset), std::string::npos);
  EXPECT_NE(output.find("Custom color"), std::string::npos);
}

TEST_F(LoggerTest, ConsoleOmitsColorWhenExplicitlyDisabled) {
  LOGGER.setColorMode(ColorMode::Disabled);
  LOGGER_LOG(LogLevel::Info, "Plain console message");

  const std::string output = capturedCout.str();
  EXPECT_NE(output.find("Plain console message"), std::string::npos);
  EXPECT_EQ(output.find("\033["), std::string::npos);
}

TEST_F(LoggerTest, ConsoleCanWriteToStderr) {
  Logger logger(false);
  logger.addConsoleSink(ConsoleStream::Stderr);
  logger.setColorMode(ColorMode::Disabled);

  logger.log(LogLevel::Info, "Diagnostic message", "consoleTest");

  EXPECT_EQ(capturedCout.str().find("Diagnostic message"), std::string::npos);
  EXPECT_NE(capturedCerr.str().find("Diagnostic message"), std::string::npos);
}

TEST(ConsoleColorPolicyTest, AutomaticColorRequiresSupportedTerminal) {
  using cppcolorlogger_detail::resolveColorEnabled;

  EXPECT_TRUE(resolveColorEnabled(ColorMode::Automatic, true, false));
  EXPECT_FALSE(resolveColorEnabled(ColorMode::Automatic, false, false));
  EXPECT_FALSE(resolveColorEnabled(ColorMode::Automatic, true, true));
}

TEST(ConsoleColorPolicyTest, ExplicitSettingOverridesAutomaticPolicy) {
  using cppcolorlogger_detail::resolveColorEnabled;

  EXPECT_TRUE(resolveColorEnabled(ColorMode::Enabled, false, true));
  EXPECT_FALSE(resolveColorEnabled(ColorMode::Disabled, true, false));
}

TEST(ConsoleColorPolicyTest, NoColorRequiresANonEmptyValue) {
  using cppcolorlogger_detail::hasNoColorValue;

  EXPECT_FALSE(hasNoColorValue(nullptr));
  EXPECT_FALSE(hasNoColorValue(""));
  EXPECT_TRUE(hasNoColorValue("1"));
}

#if defined(__unix__) || defined(__APPLE__)
TEST(ConsoleColorPolicyTest, PosixTerminalDetectionUsesIsatty) {
  EXPECT_EQ(cppcolorlogger_detail::consoleSupportsColor(ConsoleStream::Stdout), ::isatty(STDOUT_FILENO) != 0);
  EXPECT_EQ(cppcolorlogger_detail::consoleSupportsColor(ConsoleStream::Stderr), ::isatty(STDERR_FILENO) != 0);
}
#endif

TEST_F(LoggerTest, OutputContainsTimestampAndClassContext) {
  ContextExample example;
  example.emit();

  const std::string output = capturedCout.str();
  EXPECT_TRUE(std::regex_search(output, std::regex("\\[[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\\]")));
  EXPECT_NE(output.find("ContextExample::emit]"), std::string::npos);
}

TEST_F(LoggerTest, UnifiedMacroDetectsFreeAndMemberFunctions) {
  logFromAutomaticFreeFunction();
  {
    AutomaticContextExample example;
    example.member();
    AutomaticContextExample::staticMember();
    example();
  }

  const std::string output = capturedCout.str();
  EXPECT_NE(output.find("logFromAutomaticFreeFunction]"), std::string::npos);
  EXPECT_NE(output.find("AutomaticContextExample::AutomaticContextExample]"), std::string::npos);
  EXPECT_NE(output.find("AutomaticContextExample::member]"), std::string::npos);
  EXPECT_NE(output.find("AutomaticContextExample::staticMember]"), std::string::npos);
  EXPECT_NE(output.find("AutomaticContextExample::operator()]"), std::string::npos);
  EXPECT_NE(output.find("AutomaticContextExample::~AutomaticContextExample]"), std::string::npos);
}

TEST_F(LoggerTest, UnifiedMacroDetectsTemplateContexts) {
  logFromAutomaticFunctionTemplate(42);
  AutomaticContextExample example;
  example.write(42);
  AutomaticRepository<int> repository;
  repository.save(42);

  const std::string output = capturedCout.str();
  EXPECT_NE(output.find("logFromAutomaticFunctionTemplate<int>]"), std::string::npos);
  EXPECT_NE(output.find("AutomaticContextExample::write<int>]"), std::string::npos);
  EXPECT_NE(output.find("AutomaticRepository<int>::save]"), std::string::npos) << output;
}

TEST_F(LoggerTest, UnifiedMacroUsesStableLambdaLabelOrExplicitContext) {
  const auto        automatic = [] { LOGGER_LOG(LogLevel::Info, "Automatic lambda"); };
  const std::string context   = "RequestHandler::onResponse";
  const auto        named     = [&context] { LOGGER_LOG_WITH_CONTEXT(LogLevel::Info, context, "Explicit lambda"); };

  automatic();
  named();

  const std::string output = capturedCout.str();
  EXPECT_NE(output.find("[<lambda>] Automatic lambda"), std::string::npos) << output;
  EXPECT_NE(output.find("[RequestHandler::onResponse] Explicit lambda"), std::string::npos);
}

TEST_F(LoggerTest, DocumentationExamplesProduceTheirDisplayedContextAndMessage) {
  const std::shared_ptr<InMemorySink> sink = LOGGER.enableInMemorySink();

  refreshCache();
  Service service;
  service.start();

  const auto automaticLambda = []() { LOGGER_LOG(LogLevel::Info, "Cache refreshed"); };
  automaticLambda();

  const auto namedLambda = []() { LOGGER_LOG_WITH_CONTEXT(LogLevel::Info, "refreshCache", "Cache refreshed"); };
  namedLambda();

  LOGGER_LOG_WITH_CONTEXT(LogLevel::Info, "UserRepository::save", "Saving user");

  const std::vector<std::string> logs = sink->getLogs();
  ASSERT_EQ(logs.size(), 5U);
  EXPECT_NE(logs[0].find("[refreshCache] Cache refreshed"), std::string::npos);
  EXPECT_NE(logs[1].find("[Service::start] Service started"), std::string::npos);
  EXPECT_NE(logs[2].find("[<lambda>] Cache refreshed"), std::string::npos);
  EXPECT_NE(logs[3].find("[refreshCache] Cache refreshed"), std::string::npos);
  EXPECT_NE(logs[4].find("[UserRepository::save] Saving user"), std::string::npos);
}

TEST(LoggerDesignTest, PublicCustomSinkReceivesMessages) {
  Logger                               logger(false);
  const std::shared_ptr<RecordingSink> sink = std::make_shared<RecordingSink>();
  logger.addSink(sink);

  logger.log(LogLevel::Info, 42, "customSinkTest");

  ASSERT_EQ(sink->messages.size(), 1U);
  EXPECT_NE(sink->messages.front().find("42"), std::string::npos);
}

TEST(LoggerDesignTest, EachSinkCanUseADifferentLogLevel) {
  Logger                               logger(false);
  const std::shared_ptr<RecordingSink> conciseSink  = std::make_shared<RecordingSink>();
  const std::shared_ptr<RecordingSink> detailedSink = std::make_shared<RecordingSink>();
  logger.setLogLevel(LogLevel::Debug);
  logger.addSink(conciseSink, LogLevel::Info);
  logger.addSink(detailedSink, LogLevel::Debug);

  EXPECT_TRUE(logger.isEnabled(LogLevel::Debug));

  logger.log(LogLevel::Debug, "Debug details");
  logger.log(LogLevel::Info, "Request completed");
  logger.log(LogLevel::Error, "Request failed");

  ASSERT_EQ(conciseSink->messages.size(), 2U);
  EXPECT_NE(conciseSink->messages[0].find("Request completed"), std::string::npos);
  EXPECT_NE(conciseSink->messages[1].find("Request failed"), std::string::npos);
  ASSERT_EQ(detailedSink->messages.size(), 3U);
  EXPECT_NE(detailedSink->messages[0].find("Debug details"), std::string::npos);
}

TEST(LoggerDesignTest, SinkWithoutALevelReceivesEveryGloballyEnabledMessage) {
  Logger                               logger(false);
  const std::shared_ptr<RecordingSink> sink = std::make_shared<RecordingSink>();
  logger.setLogLevel(LogLevel::Verbose);
  logger.addSink(sink);

  logger.log(LogLevel::Verbose, "Verbose details");

  ASSERT_EQ(sink->messages.size(), 1U);
  EXPECT_NE(sink->messages[0].find("Verbose details"), std::string::npos);
}

TEST(LoggerDesignTest, AllowedLevelsAreAppliedBeforePerSinkLevels) {
  Logger                               logger(false);
  const std::shared_ptr<RecordingSink> sink = std::make_shared<RecordingSink>();
  logger.setLogLevel(LogLevel::Debug);
  logger.setAllowedLevels({LogLevel::Error});
  logger.addSink(sink, LogLevel::Debug);

  logger.log(LogLevel::Debug, "Not allowed");
  logger.log(LogLevel::Error, "Allowed error");

  ASSERT_EQ(sink->messages.size(), 1U);
  EXPECT_NE(sink->messages[0].find("Allowed error"), std::string::npos);
}

TEST(LoggerDesignTest, ScopedSettingsRestorePerSinkLevels) {
  Logger                               logger(false);
  const std::shared_ptr<RecordingSink> conciseSink  = std::make_shared<RecordingSink>();
  const std::shared_ptr<RecordingSink> detailedSink = std::make_shared<RecordingSink>();
  logger.setLogLevel(LogLevel::Debug);
  logger.addSink(conciseSink, LogLevel::Info);

  {
    ScopedSettings settings = logger.scopedSettings();
    logger.addSink(detailedSink, LogLevel::Debug);
    logger.log(LogLevel::Debug, "Scoped debug details");
  }

  logger.log(LogLevel::Debug, "Debug after scope");
  logger.log(LogLevel::Info, "Info after scope");

  ASSERT_EQ(conciseSink->messages.size(), 1U);
  EXPECT_NE(conciseSink->messages[0].find("Info after scope"), std::string::npos);
  ASSERT_EQ(detailedSink->messages.size(), 1U);
  EXPECT_NE(detailedSink->messages[0].find("Scoped debug details"), std::string::npos);
}

TEST(LoggerDesignTest, RemovesOnlyTheSelectedSink) {
  Logger                               logger(false);
  const std::shared_ptr<RecordingSink> removedSink   = std::make_shared<RecordingSink>();
  const std::shared_ptr<RecordingSink> remainingSink = std::make_shared<RecordingSink>();
  const SinkHandle                     removedHandle = logger.addSink(removedSink);
  logger.addSink(remainingSink);
  logger.setLogLevel(LogLevel::Debug);

  logger.log(LogLevel::Info, "Before removal", "sinkTest");
  EXPECT_TRUE(logger.removeSink(removedHandle));
  logger.log(LogLevel::Debug, "After removal", "sinkTest");

  ASSERT_EQ(removedSink->messages.size(), 1U);
  EXPECT_NE(removedSink->messages[0].find("Before removal"), std::string::npos);
  ASSERT_EQ(remainingSink->messages.size(), 2U);
  EXPECT_NE(remainingSink->messages[1].find("After removal"), std::string::npos);
  EXPECT_TRUE(logger.isEnabled(LogLevel::Debug));
}

TEST(LoggerDesignTest, RemovingUnknownOrAlreadyRemovedHandleIsHarmless) {
  Logger                               logger(false);
  Logger                               anotherLogger(false);
  const std::shared_ptr<RecordingSink> sink          = std::make_shared<RecordingSink>();
  const SinkHandle                     handle        = logger.addSink(sink);
  const SinkHandle                     foreignHandle = anotherLogger.addSink(std::make_shared<RecordingSink>());

  EXPECT_TRUE(handle.isValid());
  EXPECT_FALSE(logger.removeSink(SinkHandle()));
  EXPECT_FALSE(logger.removeSink(foreignHandle));
  EXPECT_TRUE(logger.removeSink(handle));
  EXPECT_FALSE(logger.removeSink(handle));
}

TEST_F(LoggerTest, DefaultConsoleSinkCanBeRemovedAndRestored) {
  Logger           logger;
  const SinkHandle defaultConsole = logger.getDefaultConsoleSinkHandle();

  ASSERT_TRUE(defaultConsole.isValid());
  EXPECT_TRUE(logger.removeSink(defaultConsole));
  logger.log(LogLevel::Info, "No console sink", "sinkTest");

  const SinkHandle restoredConsole = logger.addConsoleSink();
  ASSERT_TRUE(restoredConsole.isValid());
  logger.setColorMode(ColorMode::Disabled);
  logger.log(LogLevel::Info, "Console restored", "sinkTest");

  const std::string output = capturedCout.str();
  EXPECT_EQ(output.find("No console sink"), std::string::npos);
  EXPECT_NE(output.find("Console restored"), std::string::npos);
}

TEST(LoggerDesignTest, ClearSinksRemovesManagedMemorySinkAndAllowsItToBeEnabledAgain) {
  Logger                              logger(false);
  const std::shared_ptr<InMemorySink> original = logger.enableInMemorySink();
  logger.log(LogLevel::Info, "Before clear", "sinkTest");

  logger.clearSinks();
  logger.log(LogLevel::Info, "After clear", "sinkTest");
  const std::shared_ptr<InMemorySink> replacement = logger.enableInMemorySink();
  logger.log(LogLevel::Info, "After enable", "sinkTest");

  ASSERT_EQ(original->getLogs().size(), 1U);
  EXPECT_NE(original->getLogs()[0].find("Before clear"), std::string::npos);
  ASSERT_EQ(replacement->getLogs().size(), 1U);
  EXPECT_NE(replacement->getLogs()[0].find("After enable"), std::string::npos);
}

TEST(LoggerDesignTest, SinkRemovalInsideScopeIsRestoredWithTheScope) {
  Logger                               logger(false);
  const std::shared_ptr<RecordingSink> sink   = std::make_shared<RecordingSink>();
  const SinkHandle                     handle = logger.addSink(sink);

  {
    ScopedSettings settings = logger.scopedSettings();
    EXPECT_TRUE(logger.removeSink(handle));
    logger.log(LogLevel::Info, "Hidden in scope", "sinkTest");
  }
  logger.log(LogLevel::Info, "Visible after scope", "sinkTest");

  ASSERT_EQ(sink->messages.size(), 1U);
  EXPECT_NE(sink->messages[0].find("Visible after scope"), std::string::npos);
}

TEST(LoggerDesignTest, RemovingSinkDuringWriteKeepsInProgressWriteAlive) {
  Logger                                   logger(false);
  const std::shared_ptr<BlockingSinkState> state        = std::make_shared<BlockingSinkState>();
  std::shared_ptr<BlockingSink>            sink         = std::make_shared<BlockingSink>(state);
  const std::weak_ptr<BlockingSink>        sinkLifetime = sink;
  const SinkHandle                         handle       = logger.addSink(sink);

  std::thread loggingThread([&logger] { logger.log(LogLevel::Info, "In progress", "sinkTest"); });
  {
    std::unique_lock<std::mutex> write_lck(state->write_mux);
    state->write_cv.wait(write_lck, [&state] { return state->writeStarted; });
  }

  EXPECT_TRUE(logger.removeSink(handle));
  sink.reset();
  EXPECT_FALSE(sinkLifetime.expired());

  {
    std::lock_guard<std::mutex> write_lck(state->write_mux);
    state->mayFinish = true;
  }
  state->write_cv.notify_all();
  loggingThread.join();

  EXPECT_TRUE(sinkLifetime.expired());
  EXPECT_FALSE(logger.removeSink(handle));
}

TEST(LoggerDesignTest, SinkHandleCannotAffectANewLoggerAtTheSameAddress) {
  typedef std::aligned_storage<sizeof(Logger), alignof(Logger)>::type LoggerStorage;
  LoggerStorage                                                       storage;

  Logger          *firstLogger = new (&storage) Logger(false);
  const SinkHandle oldHandle   = firstLogger->addSink(std::make_shared<RecordingSink>());
  firstLogger->~Logger();

  Logger                              *secondLogger = new (&storage) Logger(false);
  const std::shared_ptr<RecordingSink> secondSink   = std::make_shared<RecordingSink>();
  secondLogger->addSink(secondSink);

  EXPECT_FALSE(secondLogger->removeSink(oldHandle));
  secondLogger->log(LogLevel::Info, "Still registered", "sinkHandleTest");
  EXPECT_EQ(secondSink->messages.size(), 1U);

  secondLogger->~Logger();
}

TEST(LoggerDesignTest, StructuredSinkReceivesLevelAndColor) {
  Logger                                logger(false);
  const std::shared_ptr<StructuredSink> sink = std::make_shared<StructuredSink>();
  logger.addSink(sink);
  logger.setLevelColor(LogLevel::Warn, Color::Blue);

  logger.log(LogLevel::Warn, "Structured message", "structuredSinkTest");

  EXPECT_EQ(sink->level, LogLevel::Warn);
  EXPECT_EQ(sink->color, Color::Blue);
  EXPECT_EQ(sink->message, "Structured message");
  EXPECT_EQ(sink->context, "structuredSinkTest");
  EXPECT_TRUE(sink->fields.empty());
  EXPECT_TRUE(std::regex_match(sink->timestamp, std::regex("[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}")));
  EXPECT_NE(sink->text.find("Structured message"), std::string::npos);
}

TEST_F(LoggerTest, LoggingMacrosCaptureSourceAndEventMetadata) {
  const std::shared_ptr<StructuredSink> sink = std::make_shared<StructuredSink>();
  LOGGER.addSink(sink);
  const std::chrono::system_clock::time_point beforeLog       = std::chrono::system_clock::now();
  const unsigned                              standardLogLine = __LINE__ + 1;
  LOGGER_LOG(LogLevel::Info, "Metadata captured");
  const std::chrono::system_clock::time_point afterLog = std::chrono::system_clock::now();

  EXPECT_EQ(sink->sourceFile, __FILE__);
  EXPECT_EQ(sink->sourceLine, standardLogLine);
  EXPECT_FALSE(sink->threadId.empty());
  EXPECT_GE(sink->eventTime, beforeLog);
  EXPECT_LE(sink->eventTime, afterLog);

  const unsigned fieldsLogLine = __LINE__ + 1;
  LOGGER_LOG_FIELDS(LogLevel::Info, "Fields metadata", {{"status", "200"}});
  EXPECT_EQ(sink->sourceFile, __FILE__);
  EXPECT_EQ(sink->sourceLine, fieldsLogLine);

  const unsigned explicitContextLogLine = __LINE__ + 1;
  LOGGER_LOG_WITH_CONTEXT(LogLevel::Info, "chosenContext", "Context metadata");
  EXPECT_EQ(sink->sourceFile, __FILE__);
  EXPECT_EQ(sink->sourceLine, explicitContextLogLine);
}

TEST(LoggerDesignTest, DirectInternalLogCallOmitsSourceLocation) {
  Logger                                logger(false);
  const std::shared_ptr<StructuredSink> sink = std::make_shared<StructuredSink>();
  logger.addSink(sink);

  logger.log(LogLevel::Info, "No macro source");

  EXPECT_TRUE(sink->sourceFile.empty());
  EXPECT_EQ(sink->sourceLine, 0U);
  EXPECT_FALSE(sink->threadId.empty());
  EXPECT_NE(sink->eventTime, std::chrono::system_clock::time_point());
}

TEST(LoggerDesignTest, EntriesIdentifyTheirCreatingThread) {
  Logger                                logger(false);
  const std::shared_ptr<StructuredSink> sink = std::make_shared<StructuredSink>();
  logger.addSink(sink);

  logger.log(LogLevel::Info, "Main thread");
  const std::string mainThreadId = sink->threadId;

  std::thread worker([&logger] { logger.log(LogLevel::Info, "Worker thread"); });
  worker.join();

  EXPECT_FALSE(mainThreadId.empty());
  EXPECT_FALSE(sink->threadId.empty());
  EXPECT_NE(sink->threadId, mainThreadId);
}

TEST(LoggerDesignTest, FormatterCanRenderUtcTimeWithMilliseconds) {
  Logger                                logger(false);
  const std::shared_ptr<StructuredSink> sink = std::make_shared<StructuredSink>();
  logger.addSink(sink);
  logger.setFormatter(std::make_shared<MillisecondLogFormatter>());

  logger.log(LogLevel::Info, "Precise timestamp");

  EXPECT_TRUE(std::regex_match(sink->timestamp,
                               std::regex("[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\\.[0-9]{3}Z")));
}

TEST(LoggerDesignTest, StructuredSinkReceivesFieldsWithoutParsingText) {
  Logger                                logger(false);
  const std::shared_ptr<StructuredSink> sink = std::make_shared<StructuredSink>();
  logger.addSink(sink);
  const LogFields fields = {{"status", "200"}, {"duration_ms", "14"}};

  logger.log(LogLevel::Info, "Request completed", fields, "RequestHandler::finish");

  ASSERT_EQ(sink->fields.size(), 2U);
  EXPECT_EQ(sink->fields[0].first, "status");
  EXPECT_EQ(sink->fields[0].second, "200");
  EXPECT_EQ(sink->fields[1].first, "duration_ms");
  EXPECT_EQ(sink->fields[1].second, "14");
  EXPECT_EQ(sink->message, "Request completed");
  EXPECT_EQ(sink->context, "RequestHandler::finish");
  EXPECT_NE(sink->text.find("Request completed [status=200, duration_ms=14]"), std::string::npos);
}

TEST_F(LoggerTest, AutomaticMemberContextIsStoredAsOneReadableValue) {
  const std::shared_ptr<StructuredSink> sink = std::make_shared<StructuredSink>();
  LOGGER.addSink(sink);

  Service service;
  service.start();

  EXPECT_EQ(sink->context, "Service::start");
}

TEST(LoggerDesignTest, DirectLogCallAcceptsBracedFields) {
  Logger                                logger(false);
  const std::shared_ptr<StructuredSink> sink = std::make_shared<StructuredSink>();
  logger.addSink(sink);

  logger.log(LogLevel::Info, "Request completed", {{"status", "200"}, {"duration_ms", "14"}});

  ASSERT_EQ(sink->fields.size(), 2U);
  EXPECT_EQ(sink->fields[0], std::make_pair(std::string("status"), std::string("200")));
  EXPECT_EQ(sink->fields[1], std::make_pair(std::string("duration_ms"), std::string("14")));
}

TEST_F(LoggerTest, FieldsMacroPreservesAutomaticSourceContextAndReadableOutput) {
  LOGGER.setColorMode(ColorMode::Disabled);

  logCompletedRequestWithFields();

  const std::string output = capturedCout.str();
  EXPECT_NE(output.find("[logCompletedRequestWithFields] Request completed [status=200, duration_ms=14]"),
            std::string::npos);
}

TEST(LoggerDesignTest, CustomFormatterWorksWithExistingSinks) {
  Logger                              logger(false);
  const std::shared_ptr<InMemorySink> sink = logger.enableInMemorySink();
  logger.setFormatter(std::make_shared<CompactLogFormatter>());

  logger.log(LogLevel::Info, "Request completed", {{"status", "200"}, {"duration_ms", "14"}}, "RequestHandler::finish");
  logger.useDefaultFormatter();
  logger.log(LogLevel::Info, "Default restored", "RequestHandler::finish");

  const std::vector<std::string> logs = sink->getLogs();
  ASSERT_EQ(logs.size(), 2U);
  EXPECT_TRUE(std::regex_match(logs[0], std::regex("[0-9]{2}:[0-9]{2}:[0-9]{2} I RequestHandler::finish \\| "
                                                   "Request completed \\{status:200 duration_ms:14\\}")));
  EXPECT_TRUE(std::regex_match(logs[1], std::regex("\\[[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\\] "
                                                   "\\[INFO\\] \\[RequestHandler::finish\\] Default restored")));
}

TEST(LoggerDesignTest, ThrowingSinkDoesNotPreventLaterSinksFromWriting) {
  Logger logger(false);
  logger.addSink(std::make_shared<ThrowingSink>());
  const std::shared_ptr<RecordingSink> recordingSink = std::make_shared<RecordingSink>();
  logger.addSink(recordingSink);

  EXPECT_NO_THROW(logger.log(LogLevel::Info, "Visible in healthy sink", "sinkFailureTest"));

  ASSERT_EQ(recordingSink->messages.size(), 1U);
  EXPECT_TRUE(logger.hasError());
  EXPECT_NE(logger.getLastError().find("write failed"), std::string::npos);
  logger.clearError();
  EXPECT_FALSE(logger.hasError());
}

TEST(LoggerDesignTest, ThrowingFormatterIsReportedWithoutCallingSinks) {
  Logger                               logger(false);
  const std::shared_ptr<RecordingSink> sink = std::make_shared<RecordingSink>();
  logger.addSink(sink);
  logger.setFormatter(std::make_shared<ThrowingFormatter>());

  EXPECT_NO_THROW(logger.log(LogLevel::Info, "Cannot be formatted", "formatterFailureTest"));

  EXPECT_TRUE(sink->messages.empty());
  EXPECT_TRUE(logger.hasError());
  EXPECT_NE(logger.getLastError().find("format failed"), std::string::npos);
}

TEST(LoggerDesignTest, ThrowingSinkFlushIsReportedAndOtherSinksAreFlushed) {
  Logger logger(false);
  logger.addSink(std::make_shared<ThrowingSink>());
  logger.addSink(std::make_shared<RecordingSink>());

  EXPECT_FALSE(logger.flushAllSinks());
  EXPECT_TRUE(logger.hasError());
  EXPECT_NE(logger.getLastError().find("flush failed"), std::string::npos);
}

TEST(LoggerDesignTest, RecursiveSinkIsStoppedAtASafeDepth) {
  Logger                               logger(false);
  const std::shared_ptr<RecursiveSink> sink = std::make_shared<RecursiveSink>(logger);
  logger.addSink(sink);

  EXPECT_NO_THROW(logger.log(LogLevel::Info, "Initial message", "recursionTest"));

  EXPECT_EQ(sink->writeCount, 8);
  EXPECT_TRUE(logger.hasError());
  EXPECT_NE(logger.getLastError().find("repeatedly called"), std::string::npos);
}

TEST(LoggerDesignTest, ScopedSettingsRestoreFormatter) {
  Logger                              logger(false);
  const std::shared_ptr<InMemorySink> sink = logger.enableInMemorySink();

  {
    ScopedSettings settings = logger.scopedSettings();
    logger.setFormatter(std::make_shared<CompactLogFormatter>());
    logger.log(LogLevel::Info, "Compact", "formatTest");
  }

  logger.log(LogLevel::Info, "Default", "formatTest");

  const std::vector<std::string> logs = sink->getLogs();
  ASSERT_EQ(logs.size(), 2U);
  EXPECT_TRUE(std::regex_match(logs[0], std::regex("[0-9]{2}:[0-9]{2}:[0-9]{2} I formatTest \\| Compact")));
  EXPECT_NE(logs[1].find("] [INFO] [formatTest] Default"), std::string::npos);
}

TEST(LoggerDesignTest, FormatterUseIsSerializedAcrossThreads) {
  Logger                                      logger(false);
  const std::shared_ptr<InMemorySink>         sink              = logger.enableInMemorySink();
  const std::shared_ptr<CountingLogFormatter> countingFormatter = std::make_shared<CountingLogFormatter>();
  logger.setFormatter(countingFormatter);

  const int                threadCount       = 4;
  const int                messagesPerThread = 50;
  std::vector<std::thread> threads;
  for (int threadIndex = 0; threadIndex < threadCount; ++threadIndex) {
    threads.push_back(std::thread([&logger, messagesPerThread] {
      for (int messageIndex = 0; messageIndex < messagesPerThread; ++messageIndex)
        logger.log(LogLevel::Info, "message");
    }));
  }

  for (std::vector<std::thread>::iterator currentThread = threads.begin(); currentThread != threads.end();
       ++currentThread)
    currentThread->join();

  EXPECT_EQ(countingFormatter->getFormatCount(), threadCount * messagesPerThread);
  EXPECT_EQ(sink->getLogs().size(), static_cast<std::size_t>(threadCount * messagesPerThread));
}

TEST(LoggerDesignTest, FormatterCanBeReplacedWhileAnotherThreadLogs) {
  Logger                              logger(false);
  const std::shared_ptr<InMemorySink> sink             = logger.enableInMemorySink();
  const std::shared_ptr<LogFormatter> compactFormatter = std::make_shared<CompactLogFormatter>();
  const std::shared_ptr<LogFormatter> defaultFormatter = std::make_shared<DefaultLogFormatter>();
  std::atomic<bool>                   loggingStarted(false);
  const int                           messageCount = 200;
  logger.setFormatter(compactFormatter);

  std::thread loggingThread([&logger, &loggingStarted, messageCount] {
    loggingStarted.store(true);
    for (int messageIndex = 0; messageIndex < messageCount; ++messageIndex)
      logger.log(LogLevel::Info, "Concurrent message", "writer");
  });

  while (!loggingStarted.load())
    std::this_thread::yield();
  for (int replacementIndex = 0; replacementIndex < messageCount; ++replacementIndex)
    logger.setFormatter(replacementIndex % 2 == 0 ? compactFormatter : defaultFormatter);

  loggingThread.join();

  const std::vector<std::string> logs = sink->getLogs();
  ASSERT_EQ(logs.size(), static_cast<std::size_t>(messageCount));
  for (std::vector<std::string>::const_iterator log = logs.begin(); log != logs.end(); ++log) {
    const bool usedCompact = std::regex_match(*log, std::regex("[0-9]{2}:[0-9]{2}:[0-9]{2} I writer \\| "
                                                               "Concurrent message"));
    const bool usedDefault = log->find("] [INFO] [writer] Concurrent message") != std::string::npos;
    EXPECT_TRUE(usedCompact || usedDefault);
  }
}

TEST(LoggerDesignTest, ScopedSettingsRestoreColorMode) {
  Logger                                logger(false);
  const std::shared_ptr<StructuredSink> sink = std::make_shared<StructuredSink>();
  logger.addSink(sink);
  logger.setColorMode(ColorMode::Disabled);

  {
    ScopedSettings settings = logger.scopedSettings();
    logger.setColorMode(ColorMode::Enabled);
    logger.log(LogLevel::Info, "Colored in scope", "colorTest");
    EXPECT_EQ(sink->colorMode, ColorMode::Enabled);
  }

  logger.log(LogLevel::Info, "Disabled mode restored", "colorTest");
  EXPECT_EQ(sink->colorMode, ColorMode::Disabled);

  logger.setColorMode(ColorMode::Automatic);
  logger.log(LogLevel::Info, "Automatic mode restored", "colorTest");
  EXPECT_EQ(sink->colorMode, ColorMode::Automatic);
}

TEST(LoggerDesignTest, NestedSettingsRestoreInOrder) {
  Logger                              logger(false);
  const std::shared_ptr<InMemorySink> sink = logger.enableInMemorySink();

  {
    ScopedSettings outerSettings = logger.scopedSettings();
    logger.setLogLevel(LogLevel::Error);
    logger.log(LogLevel::Info, "Hidden outer", "nestedSettingsTest");

    {
      ScopedSettings innerSettings = logger.scopedSettings();
      logger.setLogLevel(LogLevel::Debug);
      logger.log(LogLevel::Info, "Visible inner", "nestedSettingsTest");
    }

    logger.log(LogLevel::Info, "Hidden outer again", "nestedSettingsTest");
  }
  logger.log(LogLevel::Info, "Visible restored", "nestedSettingsTest");

  const std::vector<std::string> logs = sink->getLogs();
  ASSERT_EQ(logs.size(), 2U);
  EXPECT_NE(logs[0].find("Visible inner"), std::string::npos);
  EXPECT_NE(logs[1].find("Visible restored"), std::string::npos);
}

TEST(LoggerDesignTest, OverlappingScopedSettingsAreIsolatedByThread) {
  Logger                              logger(false);
  const std::shared_ptr<InMemorySink> sink = logger.enableInMemorySink();
  logger.setLogLevel(LogLevel::Info);

  std::atomic<int> readyThreads(0);

  std::thread restrictiveThread([&logger, &readyThreads]() {
    ScopedSettings settings = logger.scopedSettings();
    logger.setLogLevel(LogLevel::Error);
    ++readyThreads;
    while (readyThreads.load() != 2)
      std::this_thread::yield();

    logger.log(LogLevel::Info, "Restrictive thread hidden", "restrictiveThread");
    logger.log(LogLevel::Error, "Restrictive thread visible", "restrictiveThread");
  });

  std::thread verboseThread([&logger, &readyThreads]() {
    ScopedSettings settings = logger.scopedSettings();
    logger.setLogLevel(LogLevel::Debug);
    ++readyThreads;
    while (readyThreads.load() != 2)
      std::this_thread::yield();

    logger.log(LogLevel::Debug, "Verbose thread visible", "verboseThread");
  });

  restrictiveThread.join();
  verboseThread.join();
  logger.log(LogLevel::Info, "Global settings preserved", "mainThread");

  const std::vector<std::string> logs = sink->getLogs();
  ASSERT_EQ(logs.size(), 3U);

  std::string combinedLogs;
  for (std::vector<std::string>::const_iterator log = logs.begin(); log != logs.end(); ++log)
    combinedLogs += *log + '\n';

  EXPECT_EQ(combinedLogs.find("Restrictive thread hidden"), std::string::npos);
  EXPECT_NE(combinedLogs.find("Restrictive thread visible"), std::string::npos);
  EXPECT_NE(combinedLogs.find("Verbose thread visible"), std::string::npos);
  EXPECT_NE(combinedLogs.find("Global settings preserved"), std::string::npos);
}

TEST(LoggerDesignTest, NestedScopedSettingsRemainLocalToTheirThread) {
  Logger                              logger(false);
  const std::shared_ptr<InMemorySink> sink = logger.enableInMemorySink();
  logger.setLogLevel(LogLevel::Info);

  std::thread worker([&logger]() {
    ScopedSettings outer = logger.scopedSettings();
    logger.setLogLevel(LogLevel::Error);
    logger.log(LogLevel::Info, "Outer hidden before nested scope", "worker");

    {
      ScopedSettings inner = logger.scopedSettings();
      logger.setLogLevel(LogLevel::Debug);
      logger.log(LogLevel::Debug, "Inner visible", "worker");
    }

    logger.log(LogLevel::Info, "Outer hidden after nested scope", "worker");
  });

  worker.join();
  logger.log(LogLevel::Info, "Global visible after worker scope", "mainThread");

  const std::vector<std::string> logs = sink->getLogs();
  ASSERT_EQ(logs.size(), 2U);
  EXPECT_NE(logs[0].find("Inner visible"), std::string::npos);
  EXPECT_NE(logs[1].find("Global visible after worker scope"), std::string::npos);
}

TEST(LoggerDesignTest, GlobalChangesSurviveWhileAnotherThreadHasScopedSettings) {
  Logger                              logger(false);
  const std::shared_ptr<InMemorySink> sink = logger.enableInMemorySink();
  logger.setLogLevel(LogLevel::Info);

  std::atomic<bool> workerScopeReady(false);
  std::atomic<bool> globalStateChanged(false);

  std::thread worker([&logger, &workerScopeReady, &globalStateChanged]() {
    ScopedSettings settings = logger.scopedSettings();
    logger.setLogLevel(LogLevel::Error);
    workerScopeReady.store(true);
    while (!globalStateChanged.load())
      std::this_thread::yield();

    logger.log(LogLevel::Info, "Worker local setting preserved", "worker");
    logger.log(LogLevel::Error, "Worker error visible", "worker");
  });

  while (!workerScopeReady.load())
    std::this_thread::yield();
  logger.setLogLevel(LogLevel::Debug);
  logger.log(LogLevel::Debug, "Global change visible", "mainThread");
  globalStateChanged.store(true);

  worker.join();
  logger.log(LogLevel::Debug, "Global change survived", "mainThread");

  const std::vector<std::string> logs = sink->getLogs();
  ASSERT_EQ(logs.size(), 3U);

  std::string combinedLogs;
  for (std::vector<std::string>::const_iterator log = logs.begin(); log != logs.end(); ++log)
    combinedLogs += *log + '\n';

  EXPECT_EQ(combinedLogs.find("Worker local setting preserved"), std::string::npos);
  EXPECT_NE(combinedLogs.find("Worker error visible"), std::string::npos);
  EXPECT_NE(combinedLogs.find("Global change visible"), std::string::npos);
  EXPECT_NE(combinedLogs.find("Global change survived"), std::string::npos);
}

TEST(LoggerDesignTest, MovedScopedSettingsRestoresItsCreatingThread) {
  Logger                              logger(false);
  const std::shared_ptr<InMemorySink> sink = logger.enableInMemorySink();
  logger.setLogLevel(LogLevel::Info);

  ScopedSettings settings = logger.scopedSettings();
  logger.setLogLevel(LogLevel::Error);
  logger.log(LogLevel::Info, "Hidden by temporary setting", "mainThread");

  // ScopedSettings is movable. Even when its destructor runs elsewhere, it
  // must remove the override from the thread that created the scope.
  std::thread cleanupThread([](ScopedSettings) {}, std::move(settings));
  cleanupThread.join();

  logger.log(LogLevel::Info, "Creating thread restored", "mainThread");

  const std::vector<std::string> logs = sink->getLogs();
  ASSERT_EQ(logs.size(), 1U);
  EXPECT_NE(logs[0].find("Creating thread restored"), std::string::npos);
}

TEST(LoggerDesignTest, DestroyingMovedOuterScopeDoesNotRemoveInnerScope) {
  Logger logger(false);
  logger.addSink(std::make_shared<RecordingSink>());
  logger.setLogLevel(LogLevel::Info);

  std::unique_ptr<ScopedSettings> outer(new ScopedSettings(logger.scopedSettings()));
  logger.setLogLevel(LogLevel::Error);

  {
    ScopedSettings inner = logger.scopedSettings();
    logger.setLogLevel(LogLevel::Debug);
    outer.reset();

    EXPECT_TRUE(logger.isEnabled(LogLevel::Debug));
  }

  EXPECT_TRUE(logger.isEnabled(LogLevel::Info));
  EXPECT_FALSE(logger.isEnabled(LogLevel::Debug));
}

TEST(LoggerDesignTest, ScopedSettingsOutlivingAThreadCannotAffectNewThreads) {
  Logger logger(false);
  logger.addSink(std::make_shared<RecordingSink>());
  logger.setLogLevel(LogLevel::Info);

  const int threadCount = 100;
  for (int index = 0; index < threadCount; ++index) {
    std::unique_ptr<ScopedSettings> retainedSettings;
    std::thread                     creator([&logger, &retainedSettings]() {
      retainedSettings.reset(new ScopedSettings(logger));
      logger.setLogLevel(LogLevel::Error);
    });
    creator.join();

    bool        replacementUsesGlobalSettings = false;
    std::thread replacement([&logger, &replacementUsesGlobalSettings]() {
      replacementUsesGlobalSettings = logger.isEnabled(LogLevel::Info);
    });
    replacement.join();

    EXPECT_TRUE(replacementUsesGlobalSettings) << "New thread used retained settings at iteration " << index;
    retainedSettings.reset();
  }
}

TEST_F(LoggerTest, ScopedSettingsUseTheSameThreadTokenAcrossTranslationUnits) {
  LOGGER.setLogLevel(LogLevel::Error);

  EXPECT_FALSE(infoIsEnabledFromAnotherTranslationUnit());
}

TEST(LoggerDesignTest, AlwaysAndVerboseThresholdSemanticsArePreserved) {
  Logger                              logger(false);
  const std::shared_ptr<InMemorySink> sink = logger.enableInMemorySink();

  logger.setLogLevel(LogLevel::Fatal);
  logger.log(LogLevel::Always, "Always passes threshold", "levelTest");
  logger.log(LogLevel::Verbose, "Verbose hidden", "levelTest");
  logger.setLogLevel(LogLevel::Verbose);
  logger.log(LogLevel::Verbose, "Verbose visible", "levelTest");

  ASSERT_EQ(sink->getLogs().size(), 2U);
  logger.setAllowedLevels({LogLevel::Error});
  logger.log(LogLevel::Always, "Always excluded by allowed-level list", "levelTest");
  EXPECT_EQ(sink->getLogs().size(), 2U);
}

TEST(LoggerDesignTest, IsEnabledUsesThresholdAndAllowedLevels) {
  Logger logger(false);
  logger.addSink(std::make_shared<RecordingSink>());
  logger.setLogLevel(LogLevel::Info);

  EXPECT_TRUE(logger.isEnabled(LogLevel::Error));
  EXPECT_TRUE(logger.isEnabled(LogLevel::Info));
  EXPECT_FALSE(logger.isEnabled(LogLevel::Debug));

  logger.setAllowedLevels({LogLevel::Error});
  EXPECT_TRUE(logger.isEnabled(LogLevel::Error));
  EXPECT_FALSE(logger.isEnabled(LogLevel::Info));

  {
    ScopedSettings settings = logger.scopedSettings();
    logger.clearAllowedLevels();
    logger.setLogLevel(LogLevel::Debug);
    EXPECT_TRUE(logger.isEnabled(LogLevel::Debug));
  }

  EXPECT_FALSE(logger.isEnabled(LogLevel::Debug));
}

TEST(LoggerDesignTest, DisabledMessageIsNotConvertedToText) {
  Logger logger(false);
  logger.setLogLevel(LogLevel::Info);
  int streamCount = 0;

  logger.log(LogLevel::Debug, StreamCountingMessage(streamCount), "eagerTest");

  EXPECT_EQ(streamCount, 0);
}

TEST(LoggerDesignTest, EnabledMessageIsNotConvertedWhenThereAreNoSinks) {
  Logger logger(false);
  int    streamCount = 0;

  logger.log(LogLevel::Info, StreamCountingMessage(streamCount), "noSinkTest");

  EXPECT_EQ(streamCount, 0);
}

TEST(LoggerDesignTest, MessageIsNotConvertedWhenEverySinkRejectsItsLevel) {
  Logger                               logger(false);
  const std::shared_ptr<RecordingSink> sink = std::make_shared<RecordingSink>();
  logger.setLogLevel(LogLevel::Debug);
  logger.addSink(sink, LogLevel::Info);
  int streamCount = 0;

  EXPECT_FALSE(logger.isEnabled(LogLevel::Debug));
  EXPECT_TRUE(logger.isEnabled(LogLevel::Info));
  logger.log(LogLevel::Debug, StreamCountingMessage(streamCount), "perSinkFilterTest");

  EXPECT_EQ(streamCount, 0);
  EXPECT_TRUE(sink->messages.empty());
}

TEST(LoggerDesignTest, IsEnabledAvoidsConstructingMessageForDisabledLevel) {
  Logger logger(false);
  logger.setLogLevel(LogLevel::Info);
  int buildCount = 0;

  if (logger.isEnabled(LogLevel::Debug))
    logger.log(LogLevel::Debug, buildCountedMessage(buildCount), "guardedTest");

  EXPECT_EQ(buildCount, 0);
}

TEST(LoggerDesignTest, IsEnabledAvoidsConstructingFilteredMessage) {
  Logger logger(false);
  logger.setLogLevel(LogLevel::Debug);
  logger.setAllowedLevels({LogLevel::Error});
  int buildCount = 0;

  if (logger.isEnabled(LogLevel::Debug))
    logger.log(LogLevel::Debug, buildCountedMessage(buildCount), "guardedFilterTest");

  EXPECT_EQ(buildCount, 0);
}

TEST(LoggerDesignTest, ReentrantCustomSinkDoesNotDeadlock) {
  Logger                               logger(false);
  const std::shared_ptr<ReentrantSink> sink = std::make_shared<ReentrantSink>(logger);
  logger.addSink(sink);

  logger.log(LogLevel::Info, "Outer message", "reentrantTest");

  EXPECT_EQ(sink->messageCount, 2);
}

TEST(LoggerDesignTest, SignatureParserHandlesSupportedCompilerFormats) {
  using cppcolorlogger_detail::normalizeFunctionSignature;

  EXPECT_EQ(normalizeFunctionSignature("void Service::start()", "start"), "Service::start");
  EXPECT_EQ(normalizeFunctionSignature("void process(const T&) [with T = int]", "process"), "process<int>");
  EXPECT_EQ(normalizeFunctionSignature("void Serializer::write(const T&) [T = int]", "write"),
            "Serializer::write<int>");
  EXPECT_EQ(normalizeFunctionSignature("void Repository<T>::save(const T&) [with T = User]", "save"),
            "Repository<User>::save");
  EXPECT_EQ(normalizeFunctionSignature("void Repository<T>::convert(const U&) [with U = std::vector<int>; T = User]",
                                       "convert"),
            "Repository<User>::convert<std::vector<int>>");
  EXPECT_EQ(normalizeFunctionSignature("void Repository<User>::convert(const U&) [U = std::pair<int, int>, T = User]",
                                       "convert"),
            "Repository<User>::convert<std::pair<int, int>>");
  EXPECT_EQ(normalizeFunctionSignature("void Functor::operator()()", "operator()"), "Functor::operator()");
  EXPECT_EQ(normalizeFunctionSignature("Value Value::operator<<(int)", "operator<<"), "Value::operator<<");
  EXPECT_EQ(normalizeFunctionSignature("public: void __cdecl Service::start(void)", "start"), "Service::start");
  EXPECT_EQ(normalizeFunctionSignature("public: __cdecl Service::Service(void)", "Service"), "Service::Service");
  EXPECT_EQ(normalizeFunctionSignature("void __cdecl `anonymous namespace'::Service::start(void)", "start"),
            "`anonymous namespace'::Service::start");
  EXPECT_EQ(normalizeFunctionSignature("public: bool __cdecl Value::operator bool(void) const", "operator bool"),
            "Value::operator bool");
}

TEST(LoggerDesignTest, TrimHelperRemovesOnlySurroundingWhitespace) {
  using cppcolorlogger_detail::trim;

  EXPECT_EQ(trim("  void run()  "), "void run()");
  EXPECT_EQ(trim("\tvalue\n"), "value");
  EXPECT_EQ(trim("   "), "");
}

TEST(LoggerDesignTest, IdentifierCharacterHelperRecognizesNameCharacters) {
  using cppcolorlogger_detail::isIdentifierCharacter;

  EXPECT_TRUE(isIdentifierCharacter('A'));
  EXPECT_TRUE(isIdentifierCharacter('7'));
  EXPECT_TRUE(isIdentifierCharacter('_'));
  EXPECT_FALSE(isIdentifierCharacter('-'));
}

TEST(LoggerDesignTest, IdentifierHelperAcceptsOnlySimpleCppNames) {
  using cppcolorlogger_detail::isIdentifier;

  EXPECT_TRUE(isIdentifier("T"));
  EXPECT_TRUE(isIdentifier("value_1"));
  EXPECT_FALSE(isIdentifier(""));
  EXPECT_FALSE(isIdentifier("7value"));
  EXPECT_FALSE(isIdentifier("std::string"));
  EXPECT_FALSE(isIdentifier("const T"));
}

TEST(LoggerDesignTest, SplitTemplateBindingsHelperPreservesNestedCommas) {
  using cppcolorlogger_detail::splitTemplateBindings;

  const std::vector<std::string> clangBindings = splitTemplateBindings("U = std::pair<int, int>, T = User");
  ASSERT_EQ(clangBindings.size(), 2U);
  EXPECT_EQ(clangBindings[0], "U = std::pair<int, int>");
  EXPECT_EQ(clangBindings[1], "T = User");

  const std::vector<std::string> gccBindings = splitTemplateBindings("U = std::vector<int>; T = User");
  ASSERT_EQ(gccBindings.size(), 2U);
  EXPECT_EQ(gccBindings[0], "U = std::vector<int>");
  EXPECT_EQ(gccBindings[1], "T = User");
}

TEST(LoggerDesignTest, TemplateBindingStoresNameAndResolvedValue) {
  const cppcolorlogger_detail::TemplateBinding binding = {"T", "User"};

  EXPECT_EQ(binding.name, "T");
  EXPECT_EQ(binding.value, "User");
}

TEST(LoggerDesignTest, RemoveTemplateSuffixHelperSeparatesSignatureAndBindings) {
  using cppcolorlogger_detail::removeTemplateSuffix;

  std::string                                               signature = "void process(T) [with T = int]";
  const std::vector<cppcolorlogger_detail::TemplateBinding> bindings  = removeTemplateSuffix(signature);

  EXPECT_EQ(signature, "void process(T)");
  ASSERT_EQ(bindings.size(), 1U);
  EXPECT_EQ(bindings[0].name, "T");
  EXPECT_EQ(bindings[0].value, "int");
}

TEST(LoggerDesignTest, ReplaceIdentifierHelperChangesOnlyCompleteTokens) {
  using cppcolorlogger_detail::replaceIdentifier;

  std::string text = "Repository<T>::save";
  EXPECT_TRUE(replaceIdentifier(text, "T", "User"));
  EXPECT_EQ(text, "Repository<User>::save");

  std::string longerName = "Type";
  EXPECT_FALSE(replaceIdentifier(longerName, "T", "User"));
  EXPECT_EQ(longerName, "Type");
  EXPECT_FALSE(replaceIdentifier(text, "Missing", "Value"));
}

TEST(LoggerDesignTest, FindNameStartHelperKeepsConversionOperatorName) {
  using cppcolorlogger_detail::findNameStart;

  const std::string            prefix    = "public: bool __cdecl Value::operator bool";
  const std::string::size_type opStart   = prefix.rfind("operator");
  const std::string::size_type nameStart = findNameStart(prefix, opStart);

  EXPECT_EQ(prefix.substr(nameStart), "Value::operator bool");
}

TEST(LoggerDesignTest, ConsumeClassTemplateArgumentHelperFindsResolvedClassType) {
  using cppcolorlogger_detail::consumeClassTemplateArgument;

  std::string classQualifier = "Repository<User>";
  EXPECT_TRUE(consumeClassTemplateArgument(classQualifier, "User"));
  EXPECT_EQ(classQualifier, "Repository<####>");
  EXPECT_FALSE(consumeClassTemplateArgument(classQualifier, "User"));
  EXPECT_FALSE(consumeClassTemplateArgument(classQualifier, "Missing"));
}

TEST(LoggerDesignTest, ExtractFunctionNameHelperRemovesDeclarationDetails) {
  using cppcolorlogger_detail::extractFunctionName;

  EXPECT_EQ(extractFunctionName("void Service::start(int)", "start"), "Service::start");
  EXPECT_EQ(extractFunctionName("bool Predicate::operator()(int) const", "operator()"), "Predicate::operator()");
  EXPECT_EQ(extractFunctionName("main()::<lambda()>", "operator()"), "<lambda>");
  EXPECT_EQ(extractFunctionName("void lambdaProcessor()", "lambdaProcessor"), "lambdaProcessor");
  EXPECT_EQ(extractFunctionName("", "fallback"), "fallback");
}

TEST(LoggerDesignTest, SignatureParserHandlesFallbackAndLambda) {
  using cppcolorlogger_detail::normalizeFunctionSignature;

  EXPECT_EQ(normalizeFunctionSignature("void process(T) [with T = int]", "process"), "process<int>");
  EXPECT_EQ(normalizeFunctionSignature("unsupportedFunction", "unsupportedFunction"), "unsupportedFunction");
  EXPECT_EQ(normalizeFunctionSignature("main()::<lambda()>", "operator()"), "<lambda>");
  EXPECT_EQ(normalizeFunctionSignature("", "fallbackFunction"), "fallbackFunction");
}

TEST(LoggerDesignTest, MemorySinkIsSafeForConcurrentLogging) {
  Logger                              logger(false);
  const std::shared_ptr<InMemorySink> sink = logger.enableInMemorySink();
  logger.setLogLevel(LogLevel::Verbose);

  const int                threadCount       = 8;
  const int                messagesPerThread = 100;
  std::vector<std::thread> threads;
  for (int thread = 0; thread < threadCount; ++thread) {
    threads.push_back(std::thread([&logger, messagesPerThread]() {
      for (int message = 0; message < messagesPerThread; ++message)
        logger.log(LogLevel::Debug, message, "worker");
    }));
  }
  for (std::vector<std::thread>::iterator thread = threads.begin(); thread != threads.end(); ++thread)
    thread->join();

  EXPECT_EQ(sink->getLogs().size(), static_cast<std::size_t>(threadCount * messagesPerThread));
}

TEST(LoggerDesignTest, HeaderLinksAcrossTranslationUnits) {
  EXPECT_TRUE(colorsAreAvailableFromAnotherTranslationUnit());
}

TEST(LoggerDesignTest, HeaderDoesNotRemoveAnExistingErrorMacro) {
  EXPECT_TRUE(errorMacroRemainsDefinedAfterIncludingLogger());
}

} // namespace
