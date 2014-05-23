# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'variables': {
    'chromium_code': 1,
  },
  'targets': [
    {
      'target_name': 'views_content_client',
      'type': '<(component)',
      'dependencies': [
        '../../base/base.gyp:base',
        '../../base/base.gyp:base_i18n',
        '../../content/content.gyp:content',
        '../../content/content_shell_and_tests.gyp:content_shell_lib',
        '../../third_party/icu/icu.gyp:icui18n',
        '../../third_party/icu/icu.gyp:icuuc',
        '../aura/aura.gyp:aura',
        '../base/ui_base.gyp:ui_base',
        '../events/events.gyp:events',
        '../gfx/gfx.gyp:gfx',
        '../gfx/gfx.gyp:gfx_geometry',
        '../resources/ui_resources.gyp:ui_resources',
        '../resources/ui_resources.gyp:ui_test_pak',
        '../views/views.gyp:views',
        '../views/views.gyp:views_test_support',
      ],
      'defines': [
        'VIEWS_CONTENT_CLIENT_IMPLEMENTATION',
      ],
      'sources': [
        'views_content_browser_client.cc',
        'views_content_browser_client.h',
        'views_content_client.cc',
        'views_content_client.h',
        'views_content_client_export.h',
        'views_content_client_main_parts.cc',
        'views_content_client_main_parts.h',
        'views_content_main_delegate.cc',
        'views_main_delegate.h',
      ],
    },  # target_name: views_content_client
  ],
}
