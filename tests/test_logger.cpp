#include "cppColorLogger/logger.h"

#include <atomic>
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
  LOGGER_LOG(LogLevel::INFO, "Cache refreshed");
}

class Service {
public:
  void start() {
    LOGGER_LOG(LogLevel::INFO, "Service started");
  }
};

namespace {

class LoggerTest : public ::testing::Test {
protected:
  const std::string logFile = "test_log_output.txt";
  std::stringstream capturedCout;
  std::streambuf   *oldCout = nullptr;

  void SetUp() override {
    std::remove(logFile.c_str());
    LOGGER.pushLogSetting();
    LOGGER.setLogLevel(LogLevel::INFO);
    LOGGER.clearFilterLevels();
    oldCout = std::cout.rdbuf(capturedCout.rdbuf());
  }

  void TearDown() override {
    std::cout.rdbuf(oldCout);
    LOGGER.popLogSetting();
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
  void write(const std::string &message) override {
    messages.push_back(message);
  }

  std::vector<std::string> messages;
};

class StructuredSink : public LogSink {
public:
  void write(const std::string &message) override {
    text = message;
  }

  void write(const LogEntry &entry) override {
    level = entry.level;
    color = entry.color;
    text  = entry.text;
  }

  LogLevel    level = LogLevel::ALWAYS;
  std::string color;
  std::string text;
};

class ReentrantSink : public LogSink {
public:
  explicit ReentrantSink(Logger &logger) : logger_(logger) {}

  void write(const std::string &) override {
    ++messageCount;
    if (messageCount == 1)
      logger_.log(LogLevel::INFO, "Nested message", "ReentrantSink");
  }

  int messageCount = 0;

private:
  Logger &logger_;
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

class ContextExample {
public:
  void emit() {
    LOGGER_C(LogLevel::INFO, "Class context");
  }
};

void logFromAutomaticFreeFunction() {
  LOGGER_LOG(LogLevel::INFO, "Automatic free function");
}

template <typename T> void logFromAutomaticFunctionTemplate(const T &value) {
  LOGGER_LOG(LogLevel::INFO, value);
}

class AutomaticContextExample {
public:
  AutomaticContextExample() {
    LOGGER_LOG(LogLevel::INFO, "Automatic constructor");
  }

  ~AutomaticContextExample() {
    LOGGER_LOG(LogLevel::INFO, "Automatic destructor");
  }

  void member() {
    LOGGER_LOG(LogLevel::INFO, "Automatic member");
  }

  static void staticMember() {
    LOGGER_LOG(LogLevel::INFO, "Automatic static member");
  }

  template <typename T> void write(const T &value) {
    LOGGER_LOG(LogLevel::INFO, value);
  }

  void operator()() {
    LOGGER_LOG(LogLevel::INFO, "Automatic operator");
  }
};

template <typename T> class AutomaticRepository {
public:
  void save(const T &) {
    LOGGER_LOG(LogLevel::INFO, "Automatic class template");
  }
};

TEST_F(LoggerTest, LogsToFileWithoutColor) {
  const std::shared_ptr<FileSink> sink = LOGGER.addFileSink(logFile);
  ASSERT_TRUE(sink->isOpen());

  LOGGER_F(LogLevel::INFO, "File log test");

  const std::string content = readFile();
  EXPECT_NE(content.find("File log test"), std::string::npos);
  EXPECT_EQ(content.find("\033["), std::string::npos);
}

TEST_F(LoggerTest, LogLevelThresholdWorks) {
  LOGGER.addFileSink(logFile);
  LOGGER.setLogLevel(LogLevel::ERROR);

  LOGGER_F(LogLevel::DEBUG, "This should not appear");
  LOGGER_F(LogLevel::ERROR, "This should appear");

  const std::string content = readFile();
  EXPECT_EQ(content.find("This should not appear"), std::string::npos);
  EXPECT_NE(content.find("This should appear"), std::string::npos);
}

TEST_F(LoggerTest, FilterLevelWhitelistWorks) {
  LOGGER.addFileSink(logFile);
  LOGGER.setLogLevel(LogLevel::DEBUG);
  LOGGER.setFilterLevels({LogLevel::ERROR});

  LOGGER_F(LogLevel::DEBUG, "Filtered out");
  LOGGER_F(LogLevel::ERROR, "Filtered in");

  const std::string content = readFile();
  EXPECT_EQ(content.find("Filtered out"), std::string::npos);
  EXPECT_NE(content.find("Filtered in"), std::string::npos);
}

TEST_F(LoggerTest, PushPopSettingsRestoresConfiguration) {
  LOGGER.addFileSink(logFile);
  LOGGER.setLogLevel(LogLevel::INFO);
  LOGGER.pushLogSetting();
  LOGGER.setLogLevel(LogLevel::ERROR);

  LOGGER_F(LogLevel::INFO, "This should be hidden");
  LOGGER.popLogSetting();
  LOGGER_F(LogLevel::INFO, "This should be shown");

  const std::string content = readFile();
  EXPECT_EQ(content.find("This should be hidden"), std::string::npos);
  EXPECT_NE(content.find("This should be shown"), std::string::npos);
}

TEST_F(LoggerTest, ScopedSettingsRestoresConfiguration) {
  LOGGER.addFileSink(logFile);
  LOGGER.setLogLevel(LogLevel::INFO);
  {
    ScopedSettings temporary = LOGGER.scopedSettings();
    LOGGER.setLogLevel(LogLevel::ERROR);
    LOGGER_F(LogLevel::INFO, "Hidden in scope");
  }
  LOGGER_F(LogLevel::INFO, "Visible after scope");

  const std::string content = readFile();
  EXPECT_EQ(content.find("Hidden in scope"), std::string::npos);
  EXPECT_NE(content.find("Visible after scope"), std::string::npos);
}

TEST_F(LoggerTest, InMemorySinkCapturesLog) {
  LOGGER.enableInMemorySink();
  LOGGER_F(LogLevel::INFO, "Memory captured log");

  const std::vector<std::string> logs = LOGGER.getInMemoryLogs();
  ASSERT_FALSE(logs.empty());
  EXPECT_NE(logs.back().find("Memory captured log"), std::string::npos);
}

TEST_F(LoggerTest, InMemorySinkCanBeEnabledAgainAfterSettingsPop) {
  LOGGER.pushLogSetting();
  LOGGER.enableInMemorySink();
  LOGGER.popLogSetting();

  const std::shared_ptr<InMemorySink> sink = LOGGER.enableInMemorySink();
  LOGGER_F(LogLevel::INFO, "Captured after pop");

  ASSERT_TRUE(sink);
  ASSERT_FALSE(sink->getLogs().empty());
}

TEST_F(LoggerTest, ConsoleUsesConfiguredColor) {
  std::string customColor = Color::CYAN;
  LOGGER.setLevelColor(LogLevel::INFO, customColor);
  customColor.clear();
  LOGGER_F(LogLevel::INFO, "Custom color");

  const std::string output = capturedCout.str();
  EXPECT_NE(output.find(Color::CYAN), std::string::npos);
  EXPECT_NE(output.find(Color::RESET), std::string::npos);
  EXPECT_NE(output.find("Custom color"), std::string::npos);
}

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
  const auto        automatic = [] { LOGGER_LOG(LogLevel::INFO, "Automatic lambda"); };
  const std::string context   = "RequestHandler::onResponse";
  const auto        named     = [&context] { LOGGER_LOG_WITH_CONTEXT(LogLevel::INFO, context, "Explicit lambda"); };

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

  const auto automaticLambda = []() { LOGGER_LOG(LogLevel::INFO, "Cache refreshed"); };
  automaticLambda();

  const auto namedLambda = []() { LOGGER_LOG_WITH_CONTEXT(LogLevel::INFO, "refreshCache", "Cache refreshed"); };
  namedLambda();

  LOGGER_LOG_WITH_CONTEXT(LogLevel::INFO, "UserRepository::save", "Saving user");

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

  logger.log(LogLevel::INFO, 42, "customSinkTest");

  ASSERT_EQ(sink->messages.size(), 1U);
  EXPECT_NE(sink->messages.front().find("42"), std::string::npos);
}

TEST(LoggerDesignTest, StructuredSinkReceivesLevelAndColor) {
  Logger                                logger(false);
  const std::shared_ptr<StructuredSink> sink = std::make_shared<StructuredSink>();
  logger.addSink(sink);
  logger.setLevelColor(LogLevel::WARN, Color::BLUE);

  logger.log(LogLevel::WARN, "Structured message", "structuredSinkTest");

  EXPECT_EQ(sink->level, LogLevel::WARN);
  EXPECT_EQ(sink->color, Color::BLUE);
  EXPECT_NE(sink->text.find("Structured message"), std::string::npos);
}

TEST(LoggerDesignTest, NestedSettingsRestoreInOrder) {
  Logger                              logger(false);
  const std::shared_ptr<InMemorySink> sink = logger.enableInMemorySink();

  logger.pushLogSetting();
  logger.setLogLevel(LogLevel::ERROR);
  logger.log(LogLevel::INFO, "Hidden outer", "nestedSettingsTest");

  logger.pushLogSetting();
  logger.setLogLevel(LogLevel::DEBUG);
  logger.log(LogLevel::INFO, "Visible inner", "nestedSettingsTest");
  logger.popLogSetting();

  logger.log(LogLevel::INFO, "Hidden outer again", "nestedSettingsTest");
  logger.popLogSetting();
  logger.log(LogLevel::INFO, "Visible restored", "nestedSettingsTest");

  const std::vector<std::string> logs = sink->getLogs();
  ASSERT_EQ(logs.size(), 2U);
  EXPECT_NE(logs[0].find("Visible inner"), std::string::npos);
  EXPECT_NE(logs[1].find("Visible restored"), std::string::npos);
}

TEST(LoggerDesignTest, OverlappingScopedSettingsAreIsolatedByThread) {
  Logger                              logger(false);
  const std::shared_ptr<InMemorySink> sink = logger.enableInMemorySink();
  logger.setLogLevel(LogLevel::INFO);

  std::atomic<int> readyThreads(0);

  std::thread restrictiveThread([&logger, &readyThreads]() {
    ScopedSettings settings = logger.scopedSettings();
    logger.setLogLevel(LogLevel::ERROR);
    ++readyThreads;
    while (readyThreads.load() != 2)
      std::this_thread::yield();

    logger.log(LogLevel::INFO, "Restrictive thread hidden", "restrictiveThread");
    logger.log(LogLevel::ERROR, "Restrictive thread visible", "restrictiveThread");
  });

  std::thread verboseThread([&logger, &readyThreads]() {
    ScopedSettings settings = logger.scopedSettings();
    logger.setLogLevel(LogLevel::DEBUG);
    ++readyThreads;
    while (readyThreads.load() != 2)
      std::this_thread::yield();

    logger.log(LogLevel::DEBUG, "Verbose thread visible", "verboseThread");
  });

  restrictiveThread.join();
  verboseThread.join();
  logger.log(LogLevel::INFO, "Global settings preserved", "mainThread");

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
  logger.setLogLevel(LogLevel::INFO);

  std::thread worker([&logger]() {
    ScopedSettings outer = logger.scopedSettings();
    logger.setLogLevel(LogLevel::ERROR);
    logger.log(LogLevel::INFO, "Outer hidden before nested scope", "worker");

    {
      ScopedSettings inner = logger.scopedSettings();
      logger.setLogLevel(LogLevel::DEBUG);
      logger.log(LogLevel::DEBUG, "Inner visible", "worker");
    }

    logger.log(LogLevel::INFO, "Outer hidden after nested scope", "worker");
  });

  worker.join();
  logger.log(LogLevel::INFO, "Global visible after worker scope", "mainThread");

  const std::vector<std::string> logs = sink->getLogs();
  ASSERT_EQ(logs.size(), 2U);
  EXPECT_NE(logs[0].find("Inner visible"), std::string::npos);
  EXPECT_NE(logs[1].find("Global visible after worker scope"), std::string::npos);
}

TEST(LoggerDesignTest, GlobalChangesSurviveWhileAnotherThreadHasScopedSettings) {
  Logger                              logger(false);
  const std::shared_ptr<InMemorySink> sink = logger.enableInMemorySink();
  logger.setLogLevel(LogLevel::INFO);

  std::atomic<bool> workerScopeReady(false);
  std::atomic<bool> globalStateChanged(false);

  std::thread worker([&logger, &workerScopeReady, &globalStateChanged]() {
    ScopedSettings settings = logger.scopedSettings();
    logger.setLogLevel(LogLevel::ERROR);
    workerScopeReady.store(true);
    while (!globalStateChanged.load())
      std::this_thread::yield();

    logger.log(LogLevel::INFO, "Worker local setting preserved", "worker");
    logger.log(LogLevel::ERROR, "Worker error visible", "worker");
  });

  while (!workerScopeReady.load())
    std::this_thread::yield();
  logger.setLogLevel(LogLevel::DEBUG);
  logger.log(LogLevel::DEBUG, "Global change visible", "mainThread");
  globalStateChanged.store(true);

  worker.join();
  logger.log(LogLevel::DEBUG, "Global change survived", "mainThread");

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
  logger.setLogLevel(LogLevel::INFO);

  ScopedSettings settings = logger.scopedSettings();
  logger.setLogLevel(LogLevel::ERROR);
  logger.log(LogLevel::INFO, "Hidden by temporary setting", "mainThread");

  // ScopedSettings is movable. Even when its destructor runs elsewhere, it
  // must remove the override from the thread that created the scope.
  std::thread cleanupThread([](ScopedSettings) {}, std::move(settings));
  cleanupThread.join();

  logger.log(LogLevel::INFO, "Creating thread restored", "mainThread");

  const std::vector<std::string> logs = sink->getLogs();
  ASSERT_EQ(logs.size(), 1U);
  EXPECT_NE(logs[0].find("Creating thread restored"), std::string::npos);
}

TEST(LoggerDesignTest, EmptySettingsPopIsHarmless) {
  Logger                              logger(false);
  const std::shared_ptr<InMemorySink> sink = logger.enableInMemorySink();

  logger.popLogSetting();
  logger.log(LogLevel::INFO, "Still configured", "emptyPopTest");

  EXPECT_EQ(sink->getLogs().size(), 1U);
}

TEST(LoggerDesignTest, AlwaysAndVerboseThresholdSemanticsArePreserved) {
  Logger                              logger(false);
  const std::shared_ptr<InMemorySink> sink = logger.enableInMemorySink();

  logger.setLogLevel(LogLevel::FATAL);
  logger.log(LogLevel::ALWAYS, "Always passes threshold", "levelTest");
  logger.log(LogLevel::VERBOSE, "Verbose hidden", "levelTest");
  logger.setLogLevel(LogLevel::VERBOSE);
  logger.log(LogLevel::VERBOSE, "Verbose visible", "levelTest");

  ASSERT_EQ(sink->getLogs().size(), 2U);
  logger.setFilterLevels({LogLevel::ERROR});
  logger.log(LogLevel::ALWAYS, "Always filtered by whitelist", "levelTest");
  EXPECT_EQ(sink->getLogs().size(), 2U);
}

TEST(LoggerDesignTest, IsEnabledUsesThresholdAndFilter) {
  Logger logger(false);
  logger.setLogLevel(LogLevel::INFO);

  EXPECT_TRUE(logger.isEnabled(LogLevel::ERROR));
  EXPECT_TRUE(logger.isEnabled(LogLevel::INFO));
  EXPECT_FALSE(logger.isEnabled(LogLevel::DEBUG));

  logger.setFilterLevels({LogLevel::ERROR});
  EXPECT_TRUE(logger.isEnabled(LogLevel::ERROR));
  EXPECT_FALSE(logger.isEnabled(LogLevel::INFO));

  {
    ScopedSettings settings = logger.scopedSettings();
    logger.clearFilterLevels();
    logger.setLogLevel(LogLevel::DEBUG);
    EXPECT_TRUE(logger.isEnabled(LogLevel::DEBUG));
  }

  EXPECT_FALSE(logger.isEnabled(LogLevel::DEBUG));
}

TEST(LoggerDesignTest, RejectedEagerMessageIsNotConvertedToText) {
  Logger logger(false);
  logger.setLogLevel(LogLevel::INFO);
  int streamCount = 0;

  logger.log(LogLevel::DEBUG, StreamCountingMessage(streamCount), "eagerTest");

  EXPECT_EQ(streamCount, 0);
}

TEST(LoggerDesignTest, IsEnabledAvoidsConstructingRejectedMessage) {
  Logger logger(false);
  logger.setLogLevel(LogLevel::INFO);
  int buildCount = 0;

  if (logger.isEnabled(LogLevel::DEBUG))
    logger.log(LogLevel::DEBUG, buildCountedMessage(buildCount), "guardedTest");

  EXPECT_EQ(buildCount, 0);
}

TEST(LoggerDesignTest, IsEnabledAvoidsConstructingFilteredMessage) {
  Logger logger(false);
  logger.setLogLevel(LogLevel::DEBUG);
  logger.setFilterLevels({LogLevel::ERROR});
  int buildCount = 0;

  if (logger.isEnabled(LogLevel::DEBUG))
    logger.log(LogLevel::DEBUG, buildCountedMessage(buildCount), "guardedFilterTest");

  EXPECT_EQ(buildCount, 0);
}

TEST(LoggerDesignTest, ReentrantCustomSinkDoesNotDeadlock) {
  Logger                               logger(false);
  const std::shared_ptr<ReentrantSink> sink = std::make_shared<ReentrantSink>(logger);
  logger.addSink(sink);

  logger.log(LogLevel::INFO, "Outer message", "reentrantTest");

  EXPECT_EQ(sink->messageCount, 2);
}

TEST(LoggerDesignTest, OriginalLogLevelNameRemainsCompatible) {
  Logger                              logger(false);
  const std::shared_ptr<InMemorySink> sink = logger.enableInMemorySink();
  logger.log(LOGLEVELL::INFO, "Compatibility", "compatibilityTest");

  EXPECT_EQ(sink->getLogs().size(), 1U);
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
  logger.setLogLevel(LogLevel::VERBOSE);

  const int                threadCount       = 8;
  const int                messagesPerThread = 100;
  std::vector<std::thread> threads;
  for (int thread = 0; thread < threadCount; ++thread) {
    threads.push_back(std::thread([&logger]() {
      for (int message = 0; message < messagesPerThread; ++message)
        logger.log(LogLevel::DEBUG, message, "worker");
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
