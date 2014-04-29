/** ==========================================================================
 * 2011 by KjellKod.cc. This is PUBLIC DOMAIN to use at your own risk and comes
 * with no warranties. This code is yours to share, use and modify with no
 * strings attached and no restrictions or obligations.
 * ============================================================================
 * Filename:g2LogWorker.cpp  Framework for Logging and Design By Contract
 * Created: 2011 by Kjell Hedström
 *
 * PUBLIC DOMAIN and Not under copywrite protection. First published at KjellKod.cc
 * ********************************************* */

#include "g2logworker.hpp"

#include <cassert>
#include <functional>
#include "active.hpp"
#include "g2log.hpp"
#include "g2time.hpp"
#include "g2future.hpp"
#include "crashhandler.hpp"
#include <iostream> // remove

namespace g2 {

   LogWorkerImpl::LogWorkerImpl() : _bg(kjellkod::Active::createActive()) { }

   void LogWorkerImpl::bgSave(g2::LogMessagePtr msgPtr) {
      std::unique_ptr<LogMessage> uniqueMsg(std::move(msgPtr.get()));

      for (auto& sink : _sinks) {
         LogMessage msg(*(uniqueMsg));
         sink->send(LogMessageMover(std::move(msg)));
      }

      if (_sinks.empty()) {
         std::string err_msg{"g2logworker has no sinks. Message: ["};
         err_msg.append(uniqueMsg.get()->toString()).append({"]\n"});
         std::cerr << err_msg;
      }
   }

   void LogWorkerImpl::bgFatal(FatalMessagePtr msgPtr) {
      // this will be the last message. Only the active logworker can receive a FATAL call so it's 
      // safe to shutdown logging now
      g2::internal::shutDownLogging();

      std::string signal = msgPtr.get()->signal();
      auto fatal_signal_id = msgPtr.get()->_signal_id;

      std::unique_ptr<LogMessage> uniqueMsg(std::move(msgPtr.get()));
      uniqueMsg->write().append("\nExiting after fatal event  (").append(uniqueMsg->level());
      uniqueMsg->write().append("). Exiting with signal: ").append(signal)
              .append("\nLog content flushed flushed sucessfully to sink\n\n");

      std::cerr << uniqueMsg->message() << std::flush;
      for (auto& sink : _sinks) {
         LogMessage msg(*(uniqueMsg));
         sink->send(LogMessageMover(std::move(msg)));
      }


      // This clear is absolutely necessary
      // All sinks are forced to receive the fatal message above before we continue
      _sinks.clear(); // flush all queues
      internal::exitWithDefaultSignalHandler(fatal_signal_id);
      
      // should never reach this point
      perror("g2log exited after receiving FATAL trigger. Flush message status: ");
   }

   LogWorker::~LogWorker() {
      g2::internal::shutDownLoggingForActiveOnly(this);

     // The sinks WILL automatically be cleared at exit of this destructor
     // However, the waiting below ensures that all messages until this point are taken care of
     // before any internals/LogWorkerImpl of LogWorker starts to be destroyed. 
     // i.e. this avoids a race with another thread slipping through the "shutdownLogging" and calling
     // calling ::save or ::fatal through LOG/CHECK with lambda messages and "partly deconstructed LogWorkerImpl"
     //
     //   Any messages put into the queue will be OK due to:
     //  *) If it is before the wait below then they will be executed
     //  *) If it is AFTER the wait below then they will be ignored and NEVER executed
     auto bg_clear_sink_call = [this] { _impl._sinks.clear(); };
     auto token_cleared = g2::spawn_task(bg_clear_sink_call, _impl._bg.get());
     token_cleared.wait();
   }
 
   void LogWorker::save(LogMessagePtr msg) {
      _impl._bg->send([this, msg] {_impl.bgSave(msg); });}

   void LogWorker::fatal(FatalMessagePtr fatal_message) {
      _impl._bg->send([this, fatal_message] {_impl.bgFatal(fatal_message); });}

   void LogWorker::addWrappedSink(std::shared_ptr<g2::internal::SinkWrapper> sink) {
      auto bg_addsink_call = [this, sink] {_impl._sinks.push_back(sink);};
      auto token_done = g2::spawn_task(bg_addsink_call, _impl._bg.get());
      token_done.wait();
   }


   g2::DefaultFileLogger LogWorker::createWithDefaultLogger(const std::string& log_prefix, const std::string& log_directory) {
      return g2::DefaultFileLogger(log_prefix, log_directory);
   }

   std::unique_ptr<LogWorker> LogWorker::createWithNoSink() {
      return std::unique_ptr<LogWorker>(new LogWorker);
   }

   DefaultFileLogger::DefaultFileLogger(const std::string& log_prefix, const std::string& log_directory)
   : worker(LogWorker::createWithNoSink())
   , sink(worker->addSink(std2::make_unique<g2::FileSink>(log_prefix, log_directory), &FileSink::fileWrite)) { }

} // g2
