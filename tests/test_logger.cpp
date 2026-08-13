#include "cppColorLogger/logger.h"

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

int multiTranslationUnitA();
int multiTranslationUnitB();

// These names and messages intentionally match the quick-start documentation.
// Keeping them outside the anonymous namespace makes the expected context
// exactly `refreshCache` and `Service::start` on supported compilers.
void refreshCache() {
  LOGGER_LOG(LOGLEVEL::INFO, "Cache refreshed");
}

void logCompletedRequestWithFields() {
  LOGGER_LOG_FIELDS(LOGLEVEL::INFO, "Request completed", {{"status", "200"}, {"duration_ms", "14"}});
}

class Service {
public:
  void start() {
    LOGGER_LOG(LOGLEVEL::INFO, "Service started");
  }
};

namespace {

class LoggerTest : public ::testing::Test {
protected:
  const std::string               logFile = "test_log_output.txt";
  std::stringstream               capturedCout;
  std::streambuf                 *oldCout = nullptr;
  std::unique_ptr<ScopedSettings> m_testSettings;

  void SetUp() override {
    std::remove(logFile.c_str());
    m_testSettings.reset(new ScopedSettings(LOGGER));
    LOGGER.setLogLevel(LOGLEVEL::INFO);
    LOGGER.clearFilterLevels();
    oldCout = std::cout.rdbuf(capturedCout.rdbuf());
  }

  void TearDown() override {
    std::cout.rdbuf(oldCout);
    m_testSettings.reset();
    std::remove(logFile.c_str());
  }

  std::string readFile() const {
    std::ifstream     file(logFile.c_str());
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
  }
};

class RecordingSink : public LogSink {
public:
  void write(const LogEntry &entry) override {
    messages.push_back(entry.text);
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

  void write(const LogEntry &) override {
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
  void write(const LogEntry &entry) override {
    level     = entry.level;
    color     = entry.color;
    colorMode = entry.colorMode;
    timestamp = entry.timestamp;
    message   = entry.message;
    function  = entry.function;
    className = entry.className;
    fields    = entry.fields;
    text      = entry.text;
  }

  LOGLEVEL    level = LOGLEVEL::ALWAYS;
  std::string color;
  ColorMode   colorMode = ColorMode::AUTOMATIC;
  std::string timestamp;
  std::string message;
  std::string function;
  std::string className;
  LogFields   fields;
  std::string text;
};

class CompactLogFormatter : public LogFormatter {
public:
  std::string formatTimestamp(std::time_t entryTime) const override {
    return formatTimeWithPattern(entryTime, "%H:%M:%S");
  }

  std::string format(const LogEntry &entry) const override {
    std::ostringstream output;
    output << entry.timestamp << ' ' << (entry.level == LOGLEVEL::INFO ? "I" : toString(entry.level)) << ' '
           << entry.function << " | " << entry.message;

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

  void write(const LogEntry &) override {
    ++messageCount;
    if (messageCount == 1)
      m_logger.log(LOGLEVEL::INFO, "Nested message", "ReentrantSink");
  }

  int messageCount = 0;

private:
  Logger &m_logger;
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
  LogEntry entry = {};
  entry.text     = text;
  return entry;
}

class ContextExample {
public:
  void emit() {
    LOGGER_LOG(LOGLEVEL::INFO, "Class context");
  }
};

void logFromAutomaticFreeFunction() {
  LOGGER_LOG(LOGLEVEL::INFO, "Automatic free function");
}

template <typename T> void logFromAutomaticFunctionTemplate(const T &value) {
  LOGGER_LOG(LOGLEVEL::INFO, value);
}

class AutomaticContextExample {
public:
  AutomaticContextExample() {
    LOGGER_LOG(LOGLEVEL::INFO, "Automatic constructor");
  }

  ~AutomaticContextExample() {
    LOGGER_LOG(LOGLEVEL::INFO, "Automatic destructor");
  }

  void member() {
    LOGGER_LOG(LOGLEVEL::INFO, "Automatic member");
  }

  static void staticMember() {
    LOGGER_LOG(LOGLEVEL::INFO, "Automatic static member");
  }

  template <typename T> void write(const T &value) {
    LOGGER_LOG(LOGLEVEL::INFO, value);
  }

  void operator()() {
    LOGGER_LOG(LOGLEVEL::INFO, "Automatic operator");
  }
};

template <typename T> class AutomaticRepository {
public:
  void save(const T &) {
    LOGGER_LOG(LOGLEVEL::INFO, "Automatic class template");
  }
};

TEST_F(LoggerTest, LogsToFileWithoutColor) {
  const std::shared_ptr<FileSink> sink = LOGGER.addFileSink(logFile);
  ASSERT_TRUE(sink->isOpen());
  LOGGER.setColorEnabled(true);

  LOGGER_LOG(LOGLEVEL::INFO, "File log test");

  const std::string content = readFile();
  EXPECT_NE(content.find("File log test"), std::string::npos);
  EXPECT_EQ(content.find("\033["), std::string::npos);
}

TEST_F(LoggerTest, FileSinkAppendModePreservesExistingContent) {
  {
    std::ofstream existingFile(logFile.c_str(), std::ios::trunc);
    existingFile << "Existing content\n";
  }

  FileSink sink(logFile, FileOpenMode::APPEND);
  ASSERT_TRUE(sink.isOpen());
  sink.write(makeFormattedEntry("Appended content"));
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

  FileSink sink(logFile, FileOpenMode::TRUNCATE);
  ASSERT_TRUE(sink.isOpen());
  sink.write(makeFormattedEntry("Replacement content"));
  ASSERT_TRUE(sink.flush());

  const std::string content = readFile();
  EXPECT_EQ(content.find("Content to remove"), std::string::npos);
  EXPECT_NE(content.find("Replacement content"), std::string::npos);
}

TEST_F(LoggerTest, LoggerFlushWritesAllActiveFileSinks) {
  Logger                          logger(false);
  const std::shared_ptr<FileSink> sink = logger.addFileSink(logFile, FileOpenMode::TRUNCATE);
  ASSERT_TRUE(sink->isOpen());

  logger.log(LOGLEVEL::INFO, "Flushed through logger", "flushTest");

  EXPECT_TRUE(logger.flush());
  EXPECT_FALSE(sink->hasError());
  EXPECT_NE(readFile().find("Flushed through logger"), std::string::npos);
}

TEST_F(LoggerTest, FileSinkReportsOpenFailureAndKeepsFirstError) {
  const std::string invalidPath = logFile + "/cannot-open.log";
  FileSink          sink(invalidPath, FileOpenMode::APPEND);

  EXPECT_FALSE(sink.isOpen());
  ASSERT_TRUE(sink.hasError());
  const std::string openError = sink.getLastError();
  EXPECT_NE(openError.find("Failed to open"), std::string::npos);
  EXPECT_NE(openError.find(invalidPath), std::string::npos);

  sink.write(makeFormattedEntry("Ignored after open failure"));
  EXPECT_FALSE(sink.flush());
  EXPECT_EQ(sink.getLastError(), openError);
}

#if defined(__linux__)
TEST(FileSinkFailureTest, ReportsFailureWhenFileStopsAcceptingWrites) {
  Logger                          logger(false);
  const std::shared_ptr<FileSink> sink = logger.addFileSink("/dev/full");
  ASSERT_TRUE(sink->isOpen());

  logger.log(LOGLEVEL::INFO, "This write cannot complete", "failureTest");

  EXPECT_TRUE(sink->hasError());
  EXPECT_NE(sink->getLastError().find("Failed to write"), std::string::npos);
  EXPECT_FALSE(logger.flush());
}
#endif

TEST_F(LoggerTest, LogLevelThresholdWorks) {
  LOGGER.addFileSink(logFile);
  LOGGER.setLogLevel(LOGLEVEL::ERROR);

  LOGGER_LOG(LOGLEVEL::DEBUG, "This should not appear");
  LOGGER_LOG(LOGLEVEL::ERROR, "This should appear");

  const std::string content = readFile();
  EXPECT_EQ(content.find("This should not appear"), std::string::npos);
  EXPECT_NE(content.find("This should appear"), std::string::npos);
}

TEST_F(LoggerTest, FilterLevelWhitelistWorks) {
  LOGGER.addFileSink(logFile);
  LOGGER.setLogLevel(LOGLEVEL::DEBUG);
  LOGGER.setFilterLevels({LOGLEVEL::ERROR});

  LOGGER_LOG(LOGLEVEL::DEBUG, "Filtered out");
  LOGGER_LOG(LOGLEVEL::ERROR, "Filtered in");

  const std::string content = readFile();
  EXPECT_EQ(content.find("Filtered out"), std::string::npos);
  EXPECT_NE(content.find("Filtered in"), std::string::npos);
}

TEST_F(LoggerTest, ScopedSettingsRestoresConfiguration) {
  LOGGER.addFileSink(logFile);
  LOGGER.setLogLevel(LOGLEVEL::INFO);
  {
    ScopedSettings temporary = LOGGER.scopedSettings();
    LOGGER.setLogLevel(LOGLEVEL::ERROR);
    LOGGER_LOG(LOGLEVEL::INFO, "Hidden in scope");
  }
  LOGGER_LOG(LOGLEVEL::INFO, "Visible after scope");

  const std::string content = readFile();
  EXPECT_EQ(content.find("Hidden in scope"), std::string::npos);
  EXPECT_NE(content.find("Visible after scope"), std::string::npos);
}

TEST_F(LoggerTest, InMemorySinkCapturesLog) {
  LOGGER.enableInMemorySink();
  LOGGER.setColorEnabled(true);
  LOGGER_LOG(LOGLEVEL::INFO, "Memory captured log");

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
  LOGGER_LOG(LOGLEVEL::INFO, "Captured after pop");

  ASSERT_TRUE(sink);
  ASSERT_FALSE(sink->getLogs().empty());
}

TEST_F(LoggerTest, ConsoleUsesConfiguredColor) {
  std::string customColor = Color::CYAN;
  LOGGER.setLevelColor(LOGLEVEL::INFO, customColor);
  LOGGER.setColorEnabled(true);
  customColor.clear();
  LOGGER_LOG(LOGLEVEL::INFO, "Custom color");

  const std::string output = capturedCout.str();
  EXPECT_NE(output.find(Color::CYAN), std::string::npos);
  EXPECT_NE(output.find(Color::RESET), std::string::npos);
  EXPECT_NE(output.find("Custom color"), std::string::npos);
}

TEST_F(LoggerTest, ConsoleOmitsColorWhenExplicitlyDisabled) {
  LOGGER.setColorEnabled(false);
  LOGGER_LOG(LOGLEVEL::INFO, "Plain console message");

  const std::string output = capturedCout.str();
  EXPECT_NE(output.find("Plain console message"), std::string::npos);
  EXPECT_EQ(output.find("\033["), std::string::npos);
}

TEST(ConsoleColorPolicyTest, AutomaticColorRequiresSupportedTerminal) {
  using cppcolorlog::detail::resolveColorEnabled;

  EXPECT_TRUE(resolveColorEnabled(ColorMode::AUTOMATIC, true, false));
  EXPECT_FALSE(resolveColorEnabled(ColorMode::AUTOMATIC, false, false));
  EXPECT_FALSE(resolveColorEnabled(ColorMode::AUTOMATIC, true, true));
}

TEST(ConsoleColorPolicyTest, ExplicitSettingOverridesAutomaticPolicy) {
  using cppcolorlog::detail::resolveColorEnabled;

  EXPECT_TRUE(resolveColorEnabled(ColorMode::ENABLED, false, true));
  EXPECT_FALSE(resolveColorEnabled(ColorMode::DISABLED, true, false));
}

TEST(ConsoleColorPolicyTest, NoColorRequiresANonEmptyValue) {
  using cppcolorlog::detail::hasNoColorValue;

  EXPECT_FALSE(hasNoColorValue(nullptr));
  EXPECT_FALSE(hasNoColorValue(""));
  EXPECT_TRUE(hasNoColorValue("1"));
}

#if defined(__unix__) || defined(__APPLE__)
TEST(ConsoleColorPolicyTest, PosixTerminalDetectionUsesIsatty) {
  EXPECT_EQ(cppcolorlog::detail::consoleSupportsColor(), ::isatty(STDOUT_FILENO) != 0);
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
  const auto        automatic = [] { LOGGER_LOG(LOGLEVEL::INFO, "Automatic lambda"); };
  const std::string context   = "RequestHandler::onResponse";
  const auto        named     = [&context] { LOGGER_LOG_WITH_CONTEXT(LOGLEVEL::INFO, context, "Explicit lambda"); };

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

  const auto automaticLambda = []() { LOGGER_LOG(LOGLEVEL::INFO, "Cache refreshed"); };
  automaticLambda();

  const auto namedLambda = []() { LOGGER_LOG_WITH_CONTEXT(LOGLEVEL::INFO, "refreshCache", "Cache refreshed"); };
  namedLambda();

  LOGGER_LOG_WITH_CONTEXT(LOGLEVEL::INFO, "UserRepository::save", "Saving user");

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

  logger.log(LOGLEVEL::INFO, 42, "customSinkTest");

  ASSERT_EQ(sink->messages.size(), 1U);
  EXPECT_NE(sink->messages.front().find("42"), std::string::npos);
}

TEST(LoggerDesignTest, RemovesOnlyTheSelectedSink) {
  Logger                               logger(false);
  const std::shared_ptr<RecordingSink> removedSink   = std::make_shared<RecordingSink>();
  const std::shared_ptr<RecordingSink> remainingSink = std::make_shared<RecordingSink>();
  const SinkHandle                     removedHandle = logger.addSink(removedSink);
  logger.addSink(remainingSink);
  logger.setLogLevel(LOGLEVEL::DEBUG);

  logger.log(LOGLEVEL::INFO, "Before removal", "sinkTest");
  EXPECT_TRUE(logger.removeSink(removedHandle));
  logger.log(LOGLEVEL::DEBUG, "After removal", "sinkTest");

  ASSERT_EQ(removedSink->messages.size(), 1U);
  EXPECT_NE(removedSink->messages[0].find("Before removal"), std::string::npos);
  ASSERT_EQ(remainingSink->messages.size(), 2U);
  EXPECT_NE(remainingSink->messages[1].find("After removal"), std::string::npos);
  EXPECT_TRUE(logger.isEnabled(LOGLEVEL::DEBUG));
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
  logger.log(LOGLEVEL::INFO, "No console sink", "sinkTest");

  const SinkHandle restoredConsole = logger.addConsoleSink();
  ASSERT_TRUE(restoredConsole.isValid());
  logger.setColorEnabled(false);
  logger.log(LOGLEVEL::INFO, "Console restored", "sinkTest");

  const std::string output = capturedCout.str();
  EXPECT_EQ(output.find("No console sink"), std::string::npos);
  EXPECT_NE(output.find("Console restored"), std::string::npos);
}

TEST(LoggerDesignTest, ClearSinksRemovesManagedMemorySinkAndAllowsItToBeEnabledAgain) {
  Logger                              logger(false);
  const std::shared_ptr<InMemorySink> original = logger.enableInMemorySink();
  logger.log(LOGLEVEL::INFO, "Before clear", "sinkTest");

  logger.clearSinks();
  logger.log(LOGLEVEL::INFO, "After clear", "sinkTest");
  const std::shared_ptr<InMemorySink> replacement = logger.enableInMemorySink();
  logger.log(LOGLEVEL::INFO, "After enable", "sinkTest");

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
    logger.log(LOGLEVEL::INFO, "Hidden in scope", "sinkTest");
  }
  logger.log(LOGLEVEL::INFO, "Visible after scope", "sinkTest");

  ASSERT_EQ(sink->messages.size(), 1U);
  EXPECT_NE(sink->messages[0].find("Visible after scope"), std::string::npos);
}

TEST(LoggerDesignTest, RemovingSinkDuringWriteKeepsInProgressWriteAlive) {
  Logger                                   logger(false);
  const std::shared_ptr<BlockingSinkState> state        = std::make_shared<BlockingSinkState>();
  std::shared_ptr<BlockingSink>            sink         = std::make_shared<BlockingSink>(state);
  const std::weak_ptr<BlockingSink>        sinkLifetime = sink;
  const SinkHandle                         handle       = logger.addSink(sink);

  std::thread loggingThread([&logger] { logger.log(LOGLEVEL::INFO, "In progress", "sinkTest"); });
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

TEST(LoggerDesignTest, StructuredSinkReceivesLevelAndColor) {
  Logger                                logger(false);
  const std::shared_ptr<StructuredSink> sink = std::make_shared<StructuredSink>();
  logger.addSink(sink);
  logger.setLevelColor(LOGLEVEL::WARN, Color::BLUE);

  logger.log(LOGLEVEL::WARN, "Structured message", "structuredSinkTest");

  EXPECT_EQ(sink->level, LOGLEVEL::WARN);
  EXPECT_EQ(sink->color, Color::BLUE);
  EXPECT_EQ(sink->message, "Structured message");
  EXPECT_EQ(sink->function, "structuredSinkTest");
  EXPECT_TRUE(sink->className.empty());
  EXPECT_TRUE(sink->fields.empty());
  EXPECT_TRUE(std::regex_match(sink->timestamp, std::regex("[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}")));
  EXPECT_NE(sink->text.find("Structured message"), std::string::npos);
}

TEST(LoggerDesignTest, StructuredSinkReceivesFieldsWithoutParsingText) {
  Logger                                logger(false);
  const std::shared_ptr<StructuredSink> sink = std::make_shared<StructuredSink>();
  logger.addSink(sink);
  const LogFields fields = {{"status", "200"}, {"duration_ms", "14"}};

  logger.log(LOGLEVEL::INFO, "Request completed", fields, "finish", "RequestHandler");

  ASSERT_EQ(sink->fields.size(), 2U);
  EXPECT_EQ(sink->fields[0].first, "status");
  EXPECT_EQ(sink->fields[0].second, "200");
  EXPECT_EQ(sink->fields[1].first, "duration_ms");
  EXPECT_EQ(sink->fields[1].second, "14");
  EXPECT_EQ(sink->message, "Request completed");
  EXPECT_EQ(sink->function, "finish");
  EXPECT_EQ(sink->className, "RequestHandler");
  EXPECT_NE(sink->text.find("Request completed [status=200, duration_ms=14]"), std::string::npos);
}

TEST(LoggerDesignTest, DirectLogCallAcceptsBracedFields) {
  Logger                                logger(false);
  const std::shared_ptr<StructuredSink> sink = std::make_shared<StructuredSink>();
  logger.addSink(sink);

  logger.log(LOGLEVEL::INFO, "Request completed", {{"status", "200"}, {"duration_ms", "14"}});

  ASSERT_EQ(sink->fields.size(), 2U);
  EXPECT_EQ(sink->fields[0], std::make_pair(std::string("status"), std::string("200")));
  EXPECT_EQ(sink->fields[1], std::make_pair(std::string("duration_ms"), std::string("14")));
}

TEST_F(LoggerTest, FieldsMacroPreservesAutomaticSourceContextAndReadableOutput) {
  LOGGER.setColorEnabled(false);

  logCompletedRequestWithFields();

  const std::string output = capturedCout.str();
  EXPECT_NE(output.find("[logCompletedRequestWithFields] Request completed [status=200, duration_ms=14]"),
            std::string::npos);
}

TEST(JsonSerializationTest, EscapesQuotesBackslashesAndControlCharacters) {
  const std::string input = "\"\\\b\f\n\r\t\x01";

  EXPECT_EQ(cppcolorlog::detail::escapeJsonString(input), "\\\"\\\\\\b\\f\\n\\r\\t\\u0001");
}

TEST(JsonSerializationTest, SerializesRawEntryValuesAndFields) {
  const LogFields fields = {{"status", "200"}, {"path", "C:\\temp"}};
  LogEntry        entry  = {};
  entry.level            = LOGLEVEL::INFO;
  entry.text             = "human-readable text";
  entry.color            = Color::GREEN;
  entry.colorMode        = ColorMode::DISABLED;
  entry.timestamp        = "2026-09-05 14:30:12";
  entry.message          = "Request \"completed\"\n";
  entry.function         = "finish";
  entry.className        = "RequestHandler";
  entry.fields           = fields;

  const std::string json = cppcolorlog::detail::serializeLogEntryToJson(entry);

  EXPECT_EQ(json, "{\"timestamp\":\"2026-09-05 14:30:12\",\"level\":\"INFO\","
                  "\"context\":\"RequestHandler::finish\",\"message\":\"Request \\\"completed\\\"\\n\","
                  "\"fields\":{\"status\":\"200\",\"path\":\"C:\\\\temp\"}}");
}

TEST(LoggerDesignTest, CustomFormatterWorksWithExistingSinks) {
  Logger                              logger(false);
  const std::shared_ptr<InMemorySink> sink = logger.enableInMemorySink();
  logger.setFormatter(std::make_shared<CompactLogFormatter>());

  logger.log(LOGLEVEL::INFO, "Request completed", {{"status", "200"}, {"duration_ms", "14"}}, "finish",
             "RequestHandler");
  logger.useDefaultFormatter();
  logger.log(LOGLEVEL::INFO, "Default restored", "finish", "RequestHandler");

  const std::vector<std::string> logs = sink->getLogs();
  ASSERT_EQ(logs.size(), 2U);
  EXPECT_TRUE(std::regex_match(logs[0], std::regex("[0-9]{2}:[0-9]{2}:[0-9]{2} I finish \\| "
                                                   "Request completed \\{status:200 duration_ms:14\\}")));
  EXPECT_TRUE(std::regex_match(logs[1], std::regex("\\[[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\\] "
                                                   "\\[INFO\\] \\[RequestHandler::finish\\] Default restored")));
}

TEST(LoggerDesignTest, ScopedSettingsRestoreFormatter) {
  Logger                              logger(false);
  const std::shared_ptr<InMemorySink> sink = logger.enableInMemorySink();

  {
    ScopedSettings settings = logger.scopedSettings();
    logger.setFormatter(std::make_shared<CompactLogFormatter>());
    logger.log(LOGLEVEL::INFO, "Compact", "formatTest");
  }

  logger.log(LOGLEVEL::INFO, "Default", "formatTest");

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
    threads.push_back(std::thread([&logger] {
      for (int messageIndex = 0; messageIndex < messagesPerThread; ++messageIndex)
        logger.log(LOGLEVEL::INFO, "message");
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

  std::thread loggingThread([&logger, &loggingStarted] {
    loggingStarted.store(true);
    for (int messageIndex = 0; messageIndex < messageCount; ++messageIndex)
      logger.log(LOGLEVEL::INFO, "Concurrent message", "writer");
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
  logger.setColorEnabled(false);

  {
    ScopedSettings settings = logger.scopedSettings();
    logger.setColorEnabled(true);
    logger.log(LOGLEVEL::INFO, "Colored in scope", "colorTest");
    EXPECT_EQ(sink->colorMode, ColorMode::ENABLED);
  }

  logger.log(LOGLEVEL::INFO, "Disabled mode restored", "colorTest");
  EXPECT_EQ(sink->colorMode, ColorMode::DISABLED);

  logger.useAutomaticColor();
  logger.log(LOGLEVEL::INFO, "Automatic mode restored", "colorTest");
  EXPECT_EQ(sink->colorMode, ColorMode::AUTOMATIC);
}

TEST(LoggerDesignTest, NestedSettingsRestoreInOrder) {
  Logger                              logger(false);
  const std::shared_ptr<InMemorySink> sink = logger.enableInMemorySink();

  {
    ScopedSettings outerSettings = logger.scopedSettings();
    logger.setLogLevel(LOGLEVEL::ERROR);
    logger.log(LOGLEVEL::INFO, "Hidden outer", "nestedSettingsTest");

    {
      ScopedSettings innerSettings = logger.scopedSettings();
      logger.setLogLevel(LOGLEVEL::DEBUG);
      logger.log(LOGLEVEL::INFO, "Visible inner", "nestedSettingsTest");
    }

    logger.log(LOGLEVEL::INFO, "Hidden outer again", "nestedSettingsTest");
  }
  logger.log(LOGLEVEL::INFO, "Visible restored", "nestedSettingsTest");

  const std::vector<std::string> logs = sink->getLogs();
  ASSERT_EQ(logs.size(), 2U);
  EXPECT_NE(logs[0].find("Visible inner"), std::string::npos);
  EXPECT_NE(logs[1].find("Visible restored"), std::string::npos);
}

TEST(LoggerDesignTest, OverlappingScopedSettingsAreIsolatedByThread) {
  Logger                              logger(false);
  const std::shared_ptr<InMemorySink> sink = logger.enableInMemorySink();
  logger.setLogLevel(LOGLEVEL::INFO);

  std::atomic<int> readyThreads(0);

  std::thread restrictiveThread([&logger, &readyThreads]() {
    ScopedSettings settings = logger.scopedSettings();
    logger.setLogLevel(LOGLEVEL::ERROR);
    ++readyThreads;
    while (readyThreads.load() != 2)
      std::this_thread::yield();

    logger.log(LOGLEVEL::INFO, "Restrictive thread hidden", "restrictiveThread");
    logger.log(LOGLEVEL::ERROR, "Restrictive thread visible", "restrictiveThread");
  });

  std::thread verboseThread([&logger, &readyThreads]() {
    ScopedSettings settings = logger.scopedSettings();
    logger.setLogLevel(LOGLEVEL::DEBUG);
    ++readyThreads;
    while (readyThreads.load() != 2)
      std::this_thread::yield();

    logger.log(LOGLEVEL::DEBUG, "Verbose thread visible", "verboseThread");
  });

  restrictiveThread.join();
  verboseThread.join();
  logger.log(LOGLEVEL::INFO, "Global settings preserved", "mainThread");

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
  logger.setLogLevel(LOGLEVEL::INFO);

  std::thread worker([&logger]() {
    ScopedSettings outer = logger.scopedSettings();
    logger.setLogLevel(LOGLEVEL::ERROR);
    logger.log(LOGLEVEL::INFO, "Outer hidden before nested scope", "worker");

    {
      ScopedSettings inner = logger.scopedSettings();
      logger.setLogLevel(LOGLEVEL::DEBUG);
      logger.log(LOGLEVEL::DEBUG, "Inner visible", "worker");
    }

    logger.log(LOGLEVEL::INFO, "Outer hidden after nested scope", "worker");
  });

  worker.join();
  logger.log(LOGLEVEL::INFO, "Global visible after worker scope", "mainThread");

  const std::vector<std::string> logs = sink->getLogs();
  ASSERT_EQ(logs.size(), 2U);
  EXPECT_NE(logs[0].find("Inner visible"), std::string::npos);
  EXPECT_NE(logs[1].find("Global visible after worker scope"), std::string::npos);
}

TEST(LoggerDesignTest, GlobalChangesSurviveWhileAnotherThreadHasScopedSettings) {
  Logger                              logger(false);
  const std::shared_ptr<InMemorySink> sink = logger.enableInMemorySink();
  logger.setLogLevel(LOGLEVEL::INFO);

  std::atomic<bool> workerScopeReady(false);
  std::atomic<bool> globalStateChanged(false);

  std::thread worker([&logger, &workerScopeReady, &globalStateChanged]() {
    ScopedSettings settings = logger.scopedSettings();
    logger.setLogLevel(LOGLEVEL::ERROR);
    workerScopeReady.store(true);
    while (!globalStateChanged.load())
      std::this_thread::yield();

    logger.log(LOGLEVEL::INFO, "Worker local setting preserved", "worker");
    logger.log(LOGLEVEL::ERROR, "Worker error visible", "worker");
  });

  while (!workerScopeReady.load())
    std::this_thread::yield();
  logger.setLogLevel(LOGLEVEL::DEBUG);
  logger.log(LOGLEVEL::DEBUG, "Global change visible", "mainThread");
  globalStateChanged.store(true);

  worker.join();
  logger.log(LOGLEVEL::DEBUG, "Global change survived", "mainThread");

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
  logger.setLogLevel(LOGLEVEL::INFO);

  ScopedSettings settings = logger.scopedSettings();
  logger.setLogLevel(LOGLEVEL::ERROR);
  logger.log(LOGLEVEL::INFO, "Hidden by temporary setting", "mainThread");

  // ScopedSettings is movable. Even when its destructor runs elsewhere, it
  // must remove the override from the thread that created the scope.
  std::thread cleanupThread([](ScopedSettings) {}, std::move(settings));
  cleanupThread.join();

  logger.log(LOGLEVEL::INFO, "Creating thread restored", "mainThread");

  const std::vector<std::string> logs = sink->getLogs();
  ASSERT_EQ(logs.size(), 1U);
  EXPECT_NE(logs[0].find("Creating thread restored"), std::string::npos);
}

TEST(LoggerDesignTest, AlwaysAndVerboseThresholdSemanticsArePreserved) {
  Logger                              logger(false);
  const std::shared_ptr<InMemorySink> sink = logger.enableInMemorySink();

  logger.setLogLevel(LOGLEVEL::FATAL);
  logger.log(LOGLEVEL::ALWAYS, "Always passes threshold", "levelTest");
  logger.log(LOGLEVEL::VERBOSE, "Verbose hidden", "levelTest");
  logger.setLogLevel(LOGLEVEL::VERBOSE);
  logger.log(LOGLEVEL::VERBOSE, "Verbose visible", "levelTest");

  ASSERT_EQ(sink->getLogs().size(), 2U);
  logger.setFilterLevels({LOGLEVEL::ERROR});
  logger.log(LOGLEVEL::ALWAYS, "Always filtered by whitelist", "levelTest");
  EXPECT_EQ(sink->getLogs().size(), 2U);
}

TEST(LoggerDesignTest, IsEnabledUsesThresholdAndFilter) {
  Logger logger(false);
  logger.setLogLevel(LOGLEVEL::INFO);

  EXPECT_TRUE(logger.isEnabled(LOGLEVEL::ERROR));
  EXPECT_TRUE(logger.isEnabled(LOGLEVEL::INFO));
  EXPECT_FALSE(logger.isEnabled(LOGLEVEL::DEBUG));

  logger.setFilterLevels({LOGLEVEL::ERROR});
  EXPECT_TRUE(logger.isEnabled(LOGLEVEL::ERROR));
  EXPECT_FALSE(logger.isEnabled(LOGLEVEL::INFO));

  {
    ScopedSettings settings = logger.scopedSettings();
    logger.clearFilterLevels();
    logger.setLogLevel(LOGLEVEL::DEBUG);
    EXPECT_TRUE(logger.isEnabled(LOGLEVEL::DEBUG));
  }

  EXPECT_FALSE(logger.isEnabled(LOGLEVEL::DEBUG));
}

TEST(LoggerDesignTest, RejectedEagerMessageIsNotConvertedToText) {
  Logger logger(false);
  logger.setLogLevel(LOGLEVEL::INFO);
  int streamCount = 0;

  logger.log(LOGLEVEL::DEBUG, StreamCountingMessage(streamCount), "eagerTest");

  EXPECT_EQ(streamCount, 0);
}

TEST(LoggerDesignTest, IsEnabledAvoidsConstructingRejectedMessage) {
  Logger logger(false);
  logger.setLogLevel(LOGLEVEL::INFO);
  int buildCount = 0;

  if (logger.isEnabled(LOGLEVEL::DEBUG))
    logger.log(LOGLEVEL::DEBUG, buildCountedMessage(buildCount), "guardedTest");

  EXPECT_EQ(buildCount, 0);
}

TEST(LoggerDesignTest, IsEnabledAvoidsConstructingFilteredMessage) {
  Logger logger(false);
  logger.setLogLevel(LOGLEVEL::DEBUG);
  logger.setFilterLevels({LOGLEVEL::ERROR});
  int buildCount = 0;

  if (logger.isEnabled(LOGLEVEL::DEBUG))
    logger.log(LOGLEVEL::DEBUG, buildCountedMessage(buildCount), "guardedFilterTest");

  EXPECT_EQ(buildCount, 0);
}

TEST(LoggerDesignTest, ReentrantCustomSinkDoesNotDeadlock) {
  Logger                               logger(false);
  const std::shared_ptr<ReentrantSink> sink = std::make_shared<ReentrantSink>(logger);
  logger.addSink(sink);

  logger.log(LOGLEVEL::INFO, "Outer message", "reentrantTest");

  EXPECT_EQ(sink->messageCount, 2);
}

TEST(LoggerDesignTest, SignatureParserHandlesSupportedCompilerFormats) {
  using cppcolorlog::detail::normalizeFunctionSignature;

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
  using cppcolorlog::detail::trim;

  EXPECT_EQ(trim("  void run()  "), "void run()");
  EXPECT_EQ(trim("\tvalue\n"), "value");
  EXPECT_EQ(trim("   "), "");
}

TEST(LoggerDesignTest, IdentifierCharacterHelperRecognizesNameCharacters) {
  using cppcolorlog::detail::isIdentifierCharacter;

  EXPECT_TRUE(isIdentifierCharacter('A'));
  EXPECT_TRUE(isIdentifierCharacter('7'));
  EXPECT_TRUE(isIdentifierCharacter('_'));
  EXPECT_FALSE(isIdentifierCharacter('-'));
}

TEST(LoggerDesignTest, IdentifierHelperAcceptsOnlySimpleCppNames) {
  using cppcolorlog::detail::isIdentifier;

  EXPECT_TRUE(isIdentifier("T"));
  EXPECT_TRUE(isIdentifier("value_1"));
  EXPECT_FALSE(isIdentifier(""));
  EXPECT_FALSE(isIdentifier("7value"));
  EXPECT_FALSE(isIdentifier("std::string"));
  EXPECT_FALSE(isIdentifier("const T"));
}

TEST(LoggerDesignTest, SplitTemplateBindingsHelperPreservesNestedCommas) {
  using cppcolorlog::detail::splitTemplateBindings;

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
  const cppcolorlog::detail::TemplateBinding binding = {"T", "User"};

  EXPECT_EQ(binding.name, "T");
  EXPECT_EQ(binding.value, "User");
}

TEST(LoggerDesignTest, RemoveTemplateSuffixHelperSeparatesSignatureAndBindings) {
  using cppcolorlog::detail::removeTemplateSuffix;

  std::string                                             signature = "void process(T) [with T = int]";
  const std::vector<cppcolorlog::detail::TemplateBinding> bindings  = removeTemplateSuffix(signature);

  EXPECT_EQ(signature, "void process(T)");
  ASSERT_EQ(bindings.size(), 1U);
  EXPECT_EQ(bindings[0].name, "T");
  EXPECT_EQ(bindings[0].value, "int");
}

TEST(LoggerDesignTest, ReplaceIdentifierHelperChangesOnlyCompleteTokens) {
  using cppcolorlog::detail::replaceIdentifier;

  std::string text = "Repository<T>::save";
  EXPECT_TRUE(replaceIdentifier(text, "T", "User"));
  EXPECT_EQ(text, "Repository<User>::save");

  std::string longerName = "Type";
  EXPECT_FALSE(replaceIdentifier(longerName, "T", "User"));
  EXPECT_EQ(longerName, "Type");
  EXPECT_FALSE(replaceIdentifier(text, "Missing", "Value"));
}

TEST(LoggerDesignTest, FindNameStartHelperKeepsConversionOperatorName) {
  using cppcolorlog::detail::findNameStart;

  const std::string            prefix    = "public: bool __cdecl Value::operator bool";
  const std::string::size_type opStart   = prefix.rfind("operator");
  const std::string::size_type nameStart = findNameStart(prefix, opStart);

  EXPECT_EQ(prefix.substr(nameStart), "Value::operator bool");
}

TEST(LoggerDesignTest, ConsumeClassTemplateArgumentHelperFindsResolvedClassType) {
  using cppcolorlog::detail::consumeClassTemplateArgument;

  std::string classQualifier = "Repository<User>";
  EXPECT_TRUE(consumeClassTemplateArgument(classQualifier, "User"));
  EXPECT_EQ(classQualifier, "Repository<####>");
  EXPECT_FALSE(consumeClassTemplateArgument(classQualifier, "User"));
  EXPECT_FALSE(consumeClassTemplateArgument(classQualifier, "Missing"));
}

TEST(LoggerDesignTest, ExtractFunctionNameHelperRemovesDeclarationDetails) {
  using cppcolorlog::detail::extractFunctionName;

  EXPECT_EQ(extractFunctionName("void Service::start(int)", "start"), "Service::start");
  EXPECT_EQ(extractFunctionName("bool Predicate::operator()(int) const", "operator()"), "Predicate::operator()");
  EXPECT_EQ(extractFunctionName("main()::<lambda()>", "operator()"), "<lambda>");
  EXPECT_EQ(extractFunctionName("", "fallback"), "fallback");
}

TEST(LoggerDesignTest, SignatureParserHandlesFallbackAndLambda) {
  using cppcolorlog::detail::normalizeFunctionSignature;

  EXPECT_EQ(normalizeFunctionSignature("void process(T) [with T = int]", "process"), "process<int>");
  EXPECT_EQ(normalizeFunctionSignature("unsupportedFunction", "unsupportedFunction"), "unsupportedFunction");
  EXPECT_EQ(normalizeFunctionSignature("main()::<lambda()>", "operator()"), "<lambda>");
  EXPECT_EQ(normalizeFunctionSignature("", "fallbackFunction"), "fallbackFunction");
}

TEST(LoggerDesignTest, MemorySinkIsSafeForConcurrentLogging) {
  Logger                              logger(false);
  const std::shared_ptr<InMemorySink> sink = logger.enableInMemorySink();
  logger.setLogLevel(LOGLEVEL::VERBOSE);

  const int                threadCount       = 8;
  const int                messagesPerThread = 100;
  std::vector<std::thread> threads;
  for (int thread = 0; thread < threadCount; ++thread) {
    threads.push_back(std::thread([&logger]() {
      for (int message = 0; message < messagesPerThread; ++message)
        logger.log(LOGLEVEL::DEBUG, message, "worker");
    }));
  }
  for (std::vector<std::thread>::iterator thread = threads.begin(); thread != threads.end(); ++thread)
    thread->join();

  EXPECT_EQ(sink->getLogs().size(), static_cast<std::size_t>(threadCount * messagesPerThread));
}

TEST(LoggerDesignTest, HeaderLinksAcrossTranslationUnits) {
  EXPECT_EQ(multiTranslationUnitA(), 1);
  EXPECT_EQ(multiTranslationUnitB(), 1);
}

} // namespace
