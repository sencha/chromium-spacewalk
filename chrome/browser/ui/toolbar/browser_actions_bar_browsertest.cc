// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/toolbar/browser_actions_bar_browsertest.h"

#include "chrome/browser/extensions/browser_action_test_util.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/extensions/extension_service.h"
#include "components/crx_file/id_util.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_builder.h"
#include "extensions/common/value_builder.h"

namespace {

scoped_refptr<const extensions::Extension> CreateExtension(
    const std::string& name,
    bool has_browser_action) {
  extensions::DictionaryBuilder manifest;
  manifest.Set("name", name).
           Set("description", "an extension").
           Set("manifest_version", 2).
           Set("version", "1.0");
  if (has_browser_action)
    manifest.Set("browser_action", extensions::DictionaryBuilder().Pass());
  return extensions::ExtensionBuilder().
      SetManifest(manifest.Pass()).
      SetID(crx_file::id_util::GenerateId(name)).
      Build();
}

}  // namespace

BrowserActionsBarBrowserTest::BrowserActionsBarBrowserTest() {
}

BrowserActionsBarBrowserTest::~BrowserActionsBarBrowserTest() {
}

void BrowserActionsBarBrowserTest::SetUpCommandLine(
    base::CommandLine* command_line) {
  BrowserActionTestUtil::DisableAnimations();
  ExtensionBrowserTest::SetUpCommandLine(command_line);
}

void BrowserActionsBarBrowserTest::SetUpOnMainThread() {
  ExtensionBrowserTest::SetUpOnMainThread();
  browser_actions_bar_.reset(new BrowserActionTestUtil(browser()));
}

void BrowserActionsBarBrowserTest::TearDownOnMainThread() {
  BrowserActionTestUtil::EnableAnimations();
  ExtensionBrowserTest::TearDownOnMainThread();
}

void BrowserActionsBarBrowserTest::LoadExtensions() {
  // Create three extensions with browser actions.
  extension_a_ = CreateExtension("alpha", true);
  extension_b_ = CreateExtension("beta", true);
  extension_c_ = CreateExtension("gamma", true);

  const extensions::Extension* extensions[] =
      { extension_a(), extension_b(), extension_c() };
  extensions::ExtensionRegistry* registry =
      extensions::ExtensionRegistry::Get(profile());
  // Add each, and verify that it is both correctly added to the extension
  // registry and to the browser actions container.
  for (size_t i = 0; i < arraysize(extensions); ++i) {
    extension_service()->AddExtension(extensions[i]);
    EXPECT_TRUE(registry->enabled_extensions().GetByID(extensions[i]->id())) <<
        extensions[i]->name();
    EXPECT_EQ(static_cast<int>(i + 1),
              browser_actions_bar_->NumberOfBrowserActions());
    EXPECT_TRUE(browser_actions_bar_->HasIcon(i));
    EXPECT_EQ(static_cast<int>(i + 1),
              browser_actions_bar()->VisibleBrowserActions());
  }
}

// Test the basic functionality.
IN_PROC_BROWSER_TEST_F(BrowserActionsBarBrowserTest, Basic) {
  // Load an extension with no browser action.
  extension_service()->AddExtension(CreateExtension("alpha", false).get());
  // This extension should not be in the model (has no browser action).
  EXPECT_EQ(0, browser_actions_bar()->NumberOfBrowserActions());

  // Load an extension with a browser action.
  extension_service()->AddExtension(CreateExtension("beta", true).get());
  EXPECT_EQ(1, browser_actions_bar()->NumberOfBrowserActions());
  EXPECT_TRUE(browser_actions_bar()->HasIcon(0));

  // Unload the extension.
  std::string id = browser_actions_bar()->GetExtensionId(0);
  UnloadExtension(id);
  EXPECT_EQ(0, browser_actions_bar()->NumberOfBrowserActions());
}
