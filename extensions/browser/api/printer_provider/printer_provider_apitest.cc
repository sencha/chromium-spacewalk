// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/json/json_string_value_serializer.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "extensions/browser/api/printer_provider/printer_provider_api.h"
#include "extensions/common/extension.h"
#include "extensions/shell/test/shell_apitest.h"
#include "extensions/test/extension_test_message_listener.h"
#include "extensions/test/result_catcher.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

using extensions::PrinterProviderAPI;

// Callback for PrinterProviderAPI::DispatchGetPrintersRequested calls.
// It appends items in |printers| to |*printers_out|. If |done| is set, it runs
// |callback|.
void AppendPrintersAndRunCallbackIfDone(base::ListValue* printers_out,
                                        const base::Closure& callback,
                                        const base::ListValue& printers,
                                        bool done) {
  for (size_t i = 0; i < printers.GetSize(); ++i) {
    const base::DictionaryValue* printer = NULL;
    EXPECT_TRUE(printers.GetDictionary(i, &printer))
        << "Found invalid printer value at index " << i << ": " << printers;
    if (printer)
      printers_out->Append(printer->DeepCopy());
  }
  if (done && !callback.is_null())
    callback.Run();
}

// Callback for PrinterProviderAPI::DispatchPrintRequested calls.
// It copies |value| to |*result| and runs |callback|.
void RecordPrintErrorAndRunCallback(PrinterProviderAPI::PrintError* result,
                                    const base::Closure& callback,
                                    PrinterProviderAPI::PrintError value) {
  *result = value;
  if (!callback.is_null())
    callback.Run();
}

// Callback for PrinterProviderAPI::DispatchGetCapabilityRequested calls.
// It saves reported |value| as JSON string to |*result| and runs |callback|.
void RecordDictAndRunCallback(std::string* result,
                              const base::Closure& callback,
                              const base::DictionaryValue& value) {
  JSONStringValueSerializer serializer(result);
  EXPECT_TRUE(serializer.Serialize(value));
  if (!callback.is_null())
    callback.Run();
}

// Tests for chrome.printerProvider API.
class PrinterProviderApiTest : public extensions::ShellApiTest {
 public:
  PrinterProviderApiTest() {}
  ~PrinterProviderApiTest() override {}

  void StartGetPrintersRequest(
      const PrinterProviderAPI::GetPrintersCallback& callback) {
    PrinterProviderAPI::GetFactoryInstance()
        ->Get(browser_context())
        ->DispatchGetPrintersRequested(callback);
  }

  void StartPrintRequest(const std::string& extension_id,
                         const PrinterProviderAPI::PrintCallback& callback) {
    PrinterProviderAPI::PrintJob job;
    job.printer_id = "printer_id";
    job.ticket_json = "{}";
    job.content_type = "content_type";
    job.document_bytes = "bytes";

    PrinterProviderAPI::GetFactoryInstance()
        ->Get(browser_context())
        ->DispatchPrintRequested(extension_id, job, callback);
  }

  void StartCapabilityRequest(
      const std::string& extension_id,
      const PrinterProviderAPI::GetCapabilityCallback& callback) {
    PrinterProviderAPI::GetFactoryInstance()
        ->Get(browser_context())
        ->DispatchGetCapabilityRequested(extension_id, "printer_id", callback);
  }

  // Loads chrome.printerProvider test app and initializes is for test
  // |test_param|.
  // When the app's background page is loaded, the app will send 'loaded'
  // message. As a response to the message it will expect string message
  // specifying the test that should be run. When the app initializes its state
  // (e.g. registers listener for a chrome.printerProvider event) it will send
  // message 'ready', at which point the test may be started.
  // If the app is successfully initialized, |*extension_id_out| will be set to
  // the loaded extension's id, otherwise it will remain unchanged.
  void InitializePrinterProviderTestApp(const std::string& app_path,
                                        const std::string& test_param,
                                        std::string* extension_id_out) {
    ExtensionTestMessageListener loaded_listener("loaded", true);
    ExtensionTestMessageListener ready_listener("ready", false);

    const extensions::Extension* extension = LoadApp(app_path);
    ASSERT_TRUE(extension);
    const std::string extension_id = extension->id();

    loaded_listener.set_extension_id(extension_id);
    ready_listener.set_extension_id(extension_id);

    ASSERT_TRUE(loaded_listener.WaitUntilSatisfied());

    loaded_listener.Reply(test_param);

    ASSERT_TRUE(ready_listener.WaitUntilSatisfied());

    *extension_id_out = extension_id;
  }

  // Runs a test for chrome.printerProvider.onPrintRequested event.
  // |test_param|: The test that should be run.
  // |expected_result|: The print result the app is expected to report.
  void RunPrintRequestTestApp(const std::string& test_param,
                              PrinterProviderAPI::PrintError expected_result) {
    extensions::ResultCatcher catcher;

    std::string extension_id;
    InitializePrinterProviderTestApp("api_test/printer_provider/request_print",
                                     test_param, &extension_id);
    if (extension_id.empty())
      return;

    base::RunLoop run_loop;
    PrinterProviderAPI::PrintError print_result;
    StartPrintRequest(extension_id,
                      base::Bind(&RecordPrintErrorAndRunCallback, &print_result,
                                 run_loop.QuitClosure()));

    ASSERT_TRUE(catcher.GetNextResult()) << catcher.message();

    run_loop.Run();
    EXPECT_EQ(expected_result, print_result);
  }

  // Runs a test for chrome.printerProvider.onGetCapabilityRequested
  // event.
  // |test_param|: The test that should be run.
  // |expected_result|: The printer capability the app is expected to report.
  void RunPrinterCapabilitiesRequestTest(const std::string& test_param,
                                         const std::string& expected_result) {
    extensions::ResultCatcher catcher;

    std::string extension_id;
    InitializePrinterProviderTestApp(
        "api_test/printer_provider/request_capability", test_param,
        &extension_id);
    if (extension_id.empty())
      return;

    base::RunLoop run_loop;
    std::string result;
    StartCapabilityRequest(
        extension_id,
        base::Bind(&RecordDictAndRunCallback, &result, run_loop.QuitClosure()));

    ASSERT_TRUE(catcher.GetNextResult()) << catcher.message();

    run_loop.Run();
    EXPECT_EQ(expected_result, result);
  }

  // Validates that set of printers reported by test apps via
  // chrome.printerProvider.onGetPritersRequested is the same as the set of
  // printers in |expected_printers|. |expected_printers| contains list of
  // printer objects formatted as a JSON string. It is assumed that the values
  // in |expoected_printers| are unique.
  void ValidatePrinterListValue(
      const base::ListValue& printers,
      const std::vector<std::string>& expected_printers) {
    ASSERT_EQ(expected_printers.size(), printers.GetSize());
    for (size_t i = 0; i < expected_printers.size(); ++i) {
      JSONStringValueSerializer serializer(expected_printers[i]);
      int error_code;
      scoped_ptr<base::Value> printer_value(
          serializer.Deserialize(&error_code, NULL));
      ASSERT_TRUE(printer_value) << "Failed to deserialize "
                                 << expected_printers[i] << ": "
                                 << "error code " << error_code;
      EXPECT_TRUE(printers.Find(*printer_value) != printers.end())
          << "Unabe to find " << *printer_value << " in " << printers;
    }
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(PrinterProviderApiTest);
};

IN_PROC_BROWSER_TEST_F(PrinterProviderApiTest, PrintJobSuccess) {
  RunPrintRequestTestApp("OK", PrinterProviderAPI::PRINT_ERROR_NONE);
}

IN_PROC_BROWSER_TEST_F(PrinterProviderApiTest, PrintJobAsyncSuccess) {
  RunPrintRequestTestApp("ASYNC_RESPONSE",
                         PrinterProviderAPI::PRINT_ERROR_NONE);
}

IN_PROC_BROWSER_TEST_F(PrinterProviderApiTest, PrintJobFailed) {
  RunPrintRequestTestApp("INVALID_TICKET",
                         PrinterProviderAPI::PRINT_ERROR_INVALID_TICKET);
}

IN_PROC_BROWSER_TEST_F(PrinterProviderApiTest, NoPrintEventListener) {
  RunPrintRequestTestApp("NO_LISTENER", PrinterProviderAPI::PRINT_ERROR_FAILED);
}

IN_PROC_BROWSER_TEST_F(PrinterProviderApiTest,
                       PrintRequestInvalidCallbackParam) {
  RunPrintRequestTestApp("INVALID_VALUE",
                         PrinterProviderAPI::PRINT_ERROR_FAILED);
}

IN_PROC_BROWSER_TEST_F(PrinterProviderApiTest, GetCapabilitySuccess) {
  RunPrinterCapabilitiesRequestTest("OK", "{\"capability\":\"value\"}");
}

IN_PROC_BROWSER_TEST_F(PrinterProviderApiTest, GetCapabilityAsyncSuccess) {
  RunPrinterCapabilitiesRequestTest("ASYNC_RESPONSE",
                                    "{\"capability\":\"value\"}");
}

IN_PROC_BROWSER_TEST_F(PrinterProviderApiTest, EmptyCapability) {
  RunPrinterCapabilitiesRequestTest("EMPTY", "{}");
}

IN_PROC_BROWSER_TEST_F(PrinterProviderApiTest, NoCapabilityEventListener) {
  RunPrinterCapabilitiesRequestTest("NO_LISTENER", "{}");
}

IN_PROC_BROWSER_TEST_F(PrinterProviderApiTest, CapabilityInvalidValue) {
  RunPrinterCapabilitiesRequestTest("INVALID_VALUE", "{}");
}

IN_PROC_BROWSER_TEST_F(PrinterProviderApiTest, GetPrintersSuccess) {
  extensions::ResultCatcher catcher;

  std::string extension_id;
  InitializePrinterProviderTestApp("api_test/printer_provider/request_printers",
                                   "OK", &extension_id);
  ASSERT_FALSE(extension_id.empty());

  base::RunLoop run_loop;
  base::ListValue printers;

  StartGetPrintersRequest(base::Bind(&AppendPrintersAndRunCallbackIfDone,
                                     &printers, run_loop.QuitClosure()));

  ASSERT_TRUE(catcher.GetNextResult()) << catcher.message();

  run_loop.Run();

  std::vector<std::string> expected_printers;
  expected_printers.push_back(base::StringPrintf(
      "{"
      "\"description\":\"Test printer\","
      "\"extensionId\":\"%s\","
      "\"id\":\"printer1\","
      "\"name\":\"Printer 1\""
      "}",
      extension_id.c_str()));
  expected_printers.push_back(base::StringPrintf(
      "{"
      "\"extensionId\":\"%s\","
      "\"id\":\"printerNoDesc\","
      "\"name\":\"Printer 2\""
      "}",
      extension_id.c_str()));

  ValidatePrinterListValue(printers, expected_printers);
}

IN_PROC_BROWSER_TEST_F(PrinterProviderApiTest, GetPrintersAsyncSuccess) {
  extensions::ResultCatcher catcher;

  std::string extension_id;
  InitializePrinterProviderTestApp("api_test/printer_provider/request_printers",
                                   "ASYNC_RESPONSE", &extension_id);
  ASSERT_FALSE(extension_id.empty());

  base::RunLoop run_loop;
  base::ListValue printers;

  StartGetPrintersRequest(base::Bind(&AppendPrintersAndRunCallbackIfDone,
                                     &printers, run_loop.QuitClosure()));

  ASSERT_TRUE(catcher.GetNextResult()) << catcher.message();

  run_loop.Run();

  std::vector<std::string> expected_printers;
  expected_printers.push_back(base::StringPrintf(
      "{"
      "\"description\":\"Test printer\","
      "\"extensionId\":\"%s\","
      "\"id\":\"printer1\","
      "\"name\":\"Printer 1\""
      "}",
      extension_id.c_str()));

  ValidatePrinterListValue(printers, expected_printers);
}

IN_PROC_BROWSER_TEST_F(PrinterProviderApiTest, GetPrintersTwoExtensions) {
  extensions::ResultCatcher catcher;

  std::string extension_id_1;
  InitializePrinterProviderTestApp("api_test/printer_provider/request_printers",
                                   "OK", &extension_id_1);
  ASSERT_FALSE(extension_id_1.empty());

  std::string extension_id_2;
  InitializePrinterProviderTestApp(
      "api_test/printer_provider/request_printers_second", "OK",
      &extension_id_2);
  ASSERT_FALSE(extension_id_2.empty());

  base::RunLoop run_loop;
  base::ListValue printers;

  StartGetPrintersRequest(base::Bind(&AppendPrintersAndRunCallbackIfDone,
                                     &printers, run_loop.QuitClosure()));

  ASSERT_TRUE(catcher.GetNextResult()) << catcher.message();
  ASSERT_TRUE(catcher.GetNextResult()) << catcher.message();

  run_loop.Run();

  std::vector<std::string> expected_printers;
  expected_printers.push_back(base::StringPrintf(
      "{"
      "\"description\":\"Test printer\","
      "\"extensionId\":\"%s\","
      "\"id\":\"printer1\","
      "\"name\":\"Printer 1\""
      "}",
      extension_id_1.c_str()));
  expected_printers.push_back(base::StringPrintf(
      "{"
      "\"extensionId\":\"%s\","
      "\"id\":\"printerNoDesc\","
      "\"name\":\"Printer 2\""
      "}",
      extension_id_1.c_str()));
  expected_printers.push_back(base::StringPrintf(
      "{"
      "\"description\":\"Test printer\","
      "\"extensionId\":\"%s\","
      "\"id\":\"printer1\","
      "\"name\":\"Printer 1\""
      "}",
      extension_id_2.c_str()));
  expected_printers.push_back(base::StringPrintf(
      "{"
      "\"extensionId\":\"%s\","
      "\"id\":\"printerNoDesc\","
      "\"name\":\"Printer 2\""
      "}",
      extension_id_2.c_str()));

  ValidatePrinterListValue(printers, expected_printers);
}

IN_PROC_BROWSER_TEST_F(PrinterProviderApiTest,
                       GetPrintersTwoExtensionsOneFails) {
  extensions::ResultCatcher catcher;

  std::string extension_id_1;
  InitializePrinterProviderTestApp("api_test/printer_provider/request_printers",
                                   "NOT_ARRAY", &extension_id_1);
  ASSERT_FALSE(extension_id_1.empty());

  std::string extension_id_2;
  InitializePrinterProviderTestApp(
      "api_test/printer_provider/request_printers_second", "OK",
      &extension_id_2);
  ASSERT_FALSE(extension_id_2.empty());

  base::RunLoop run_loop;
  base::ListValue printers;

  StartGetPrintersRequest(base::Bind(&AppendPrintersAndRunCallbackIfDone,
                                     &printers, run_loop.QuitClosure()));

  ASSERT_TRUE(catcher.GetNextResult()) << catcher.message();
  ASSERT_TRUE(catcher.GetNextResult()) << catcher.message();

  run_loop.Run();

  std::vector<std::string> expected_printers;
  expected_printers.push_back(base::StringPrintf(
      "{"
      "\"description\":\"Test printer\","
      "\"extensionId\":\"%s\","
      "\"id\":\"printer1\","
      "\"name\":\"Printer 1\""
      "}",
      extension_id_2.c_str()));
  expected_printers.push_back(base::StringPrintf(
      "{"
      "\"extensionId\":\"%s\","
      "\"id\":\"printerNoDesc\","
      "\"name\":\"Printer 2\""
      "}",
      extension_id_2.c_str()));

  ValidatePrinterListValue(printers, expected_printers);
}

IN_PROC_BROWSER_TEST_F(PrinterProviderApiTest,
                       GetPrintersTwoExtensionsOneWithNoListener) {
  extensions::ResultCatcher catcher;

  std::string extension_id_1;
  InitializePrinterProviderTestApp("api_test/printer_provider/request_printers",
                                   "NO_LISTENER", &extension_id_1);
  ASSERT_FALSE(extension_id_1.empty());

  std::string extension_id_2;
  InitializePrinterProviderTestApp(
      "api_test/printer_provider/request_printers_second", "OK",
      &extension_id_2);
  ASSERT_FALSE(extension_id_2.empty());

  base::RunLoop run_loop;
  base::ListValue printers;

  StartGetPrintersRequest(base::Bind(&AppendPrintersAndRunCallbackIfDone,
                                     &printers, run_loop.QuitClosure()));

  ASSERT_TRUE(catcher.GetNextResult()) << catcher.message();
  ASSERT_TRUE(catcher.GetNextResult()) << catcher.message();

  run_loop.Run();

  std::vector<std::string> expected_printers;
  expected_printers.push_back(base::StringPrintf(
      "{"
      "\"description\":\"Test printer\","
      "\"extensionId\":\"%s\","
      "\"id\":\"printer1\","
      "\"name\":\"Printer 1\""
      "}",
      extension_id_2.c_str()));
  expected_printers.push_back(base::StringPrintf(
      "{"
      "\"extensionId\":\"%s\","
      "\"id\":\"printerNoDesc\","
      "\"name\":\"Printer 2\""
      "}",
      extension_id_2.c_str()));

  ValidatePrinterListValue(printers, expected_printers);
}

IN_PROC_BROWSER_TEST_F(PrinterProviderApiTest, GetPrintersNoListener) {
  extensions::ResultCatcher catcher;

  std::string extension_id;
  InitializePrinterProviderTestApp("api_test/printer_provider/request_printers",
                                   "NO_LISTENER", &extension_id);
  ASSERT_FALSE(extension_id.empty());

  base::RunLoop run_loop;
  base::ListValue printers;

  StartGetPrintersRequest(base::Bind(&AppendPrintersAndRunCallbackIfDone,
                                     &printers, run_loop.QuitClosure()));

  ASSERT_TRUE(catcher.GetNextResult()) << catcher.message();

  run_loop.Run();

  EXPECT_TRUE(printers.empty());
}

IN_PROC_BROWSER_TEST_F(PrinterProviderApiTest, GetPrintersNotArray) {
  extensions::ResultCatcher catcher;

  std::string extension_id;
  InitializePrinterProviderTestApp("api_test/printer_provider/request_printers",
                                   "NOT_ARRAY", &extension_id);
  ASSERT_FALSE(extension_id.empty());

  base::RunLoop run_loop;
  base::ListValue printers;

  StartGetPrintersRequest(base::Bind(&AppendPrintersAndRunCallbackIfDone,
                                     &printers, run_loop.QuitClosure()));

  ASSERT_TRUE(catcher.GetNextResult()) << catcher.message();

  run_loop.Run();

  EXPECT_TRUE(printers.empty());
}

IN_PROC_BROWSER_TEST_F(PrinterProviderApiTest,
                       GetPrintersInvalidPrinterValueType) {
  extensions::ResultCatcher catcher;

  std::string extension_id;
  InitializePrinterProviderTestApp("api_test/printer_provider/request_printers",
                                   "INVALID_PRINTER_TYPE", &extension_id);
  ASSERT_FALSE(extension_id.empty());

  base::RunLoop run_loop;
  base::ListValue printers;

  StartGetPrintersRequest(base::Bind(&AppendPrintersAndRunCallbackIfDone,
                                     &printers, run_loop.QuitClosure()));

  ASSERT_TRUE(catcher.GetNextResult()) << catcher.message();

  run_loop.Run();

  EXPECT_TRUE(printers.empty());
}

IN_PROC_BROWSER_TEST_F(PrinterProviderApiTest, GetPrintersInvalidPrinterValue) {
  extensions::ResultCatcher catcher;

  std::string extension_id;
  InitializePrinterProviderTestApp("api_test/printer_provider/request_printers",
                                   "INVALID_PRINTER", &extension_id);
  ASSERT_FALSE(extension_id.empty());

  base::RunLoop run_loop;
  base::ListValue printers;

  StartGetPrintersRequest(base::Bind(&AppendPrintersAndRunCallbackIfDone,
                                     &printers, run_loop.QuitClosure()));

  ASSERT_TRUE(catcher.GetNextResult()) << catcher.message();

  run_loop.Run();

  EXPECT_TRUE(printers.empty());
}

}  // namespace
