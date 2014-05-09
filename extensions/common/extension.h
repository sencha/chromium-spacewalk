// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_COMMON_EXTENSION_H_
#define EXTENSIONS_COMMON_EXTENSION_H_

#include <algorithm>
#include <iosfwd>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/hash_tables.h"
#include "base/files/file_path.h"
#include "base/memory/linked_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/synchronization/lock.h"
#include "base/threading/thread_checker.h"
#include "extensions/common/extension_resource.h"
#include "extensions/common/install_warning.h"
#include "extensions/common/manifest.h"
#include "extensions/common/permissions/api_permission.h"
#include "extensions/common/url_pattern.h"
#include "extensions/common/url_pattern_set.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/gfx/size.h"
#include "url/gurl.h"

class ExtensionAction;
class SkBitmap;

namespace base {
class DictionaryValue;
class Version;
}

namespace gfx {
class ImageSkia;
}

namespace extensions {
class PermissionsData;
class APIPermissionSet;
class ManifestPermissionSet;
class PermissionSet;

// Uniquely identifies an Extension, using 32 characters from the alphabet
// 'a'-'p'.  An empty string represents "no extension".
//
// Note: If this gets used heavily in files that don't otherwise need to include
// extension.h, we should pull it into a dedicated header.
typedef std::string ExtensionId;

// Represents a Chrome extension.
// Once created, an Extension object is immutable, with the exception of its
// RuntimeData. This makes it safe to use on any thread, since access to the
// RuntimeData is protected by a lock.
class Extension : public base::RefCountedThreadSafe<Extension> {
 public:
  struct ManifestData;

  typedef std::map<const std::string, linked_ptr<ManifestData> >
      ManifestDataMap;

  enum State {
    DISABLED = 0,
    ENABLED,
    // An external extension that the user uninstalled. We should not reinstall
    // such extensions on startup.
    EXTERNAL_EXTENSION_UNINSTALLED,
    // Special state for component extensions, since they are always loaded by
    // the component loader, and should never be auto-installed on startup.
    ENABLED_COMPONENT,
    NUM_STATES
  };

  // Used to record the reason an extension was disabled.
  enum DeprecatedDisableReason {
    DEPRECATED_DISABLE_UNKNOWN,
    DEPRECATED_DISABLE_USER_ACTION,
    DEPRECATED_DISABLE_PERMISSIONS_INCREASE,
    DEPRECATED_DISABLE_RELOAD,
    DEPRECATED_DISABLE_LAST,  // Not used.
  };

  enum DisableReason {
    DISABLE_NONE = 0,
    DISABLE_USER_ACTION = 1 << 0,
    DISABLE_PERMISSIONS_INCREASE = 1 << 1,
    DISABLE_RELOAD = 1 << 2,
    DISABLE_UNSUPPORTED_REQUIREMENT = 1 << 3,
    DISABLE_SIDELOAD_WIPEOUT = 1 << 4,
    DISABLE_UNKNOWN_FROM_SYNC = 1 << 5,
    DISABLE_PERMISSIONS_CONSENT = 1 << 6,  // Unused - abandoned experiment.
    DISABLE_KNOWN_DISABLED = 1 << 7,
    DISABLE_NOT_VERIFIED = 1 << 8,  // Disabled because we could not verify
                                    // the install.
    DISABLE_GREYLIST = 1 << 9,
    DISABLE_CORRUPTED = 1 << 10,
  };

  enum InstallType {
    INSTALL_ERROR,
    DOWNGRADE,
    REINSTALL,
    UPGRADE,
    NEW_INSTALL
  };

  // A base class for parsed manifest data that APIs want to store on
  // the extension. Related to base::SupportsUserData, but with an immutable
  // thread-safe interface to match Extension.
  struct ManifestData {
    virtual ~ManifestData() {}
  };

  enum InitFromValueFlags {
    NO_FLAGS = 0,

    // Usually, the id of an extension is generated by the "key" property of
    // its manifest, but if |REQUIRE_KEY| is not set, a temporary ID will be
    // generated based on the path.
    REQUIRE_KEY = 1 << 0,

    // Requires the extension to have an up-to-date manifest version.
    // Typically, we'll support multiple manifest versions during a version
    // transition. This flag signals that we want to require the most modern
    // manifest version that Chrome understands.
    REQUIRE_MODERN_MANIFEST_VERSION = 1 << 1,

    // |ALLOW_FILE_ACCESS| indicates that the user is allowing this extension
    // to have file access. If it's not present, then permissions and content
    // scripts that match file:/// URLs will be filtered out.
    ALLOW_FILE_ACCESS = 1 << 2,

    // |FROM_WEBSTORE| indicates that the extension was installed from the
    // Chrome Web Store.
    FROM_WEBSTORE = 1 << 3,

    // |FROM_BOOKMARK| indicates the extension is a bookmark app which has been
    // generated from a web page. Bookmark apps have no permissions or extent
    // and launch the web page they are created from when run.
    FROM_BOOKMARK = 1 << 4,

    // |FOLLOW_SYMLINKS_ANYWHERE| means that resources can be symlinks to
    // anywhere in the filesystem, rather than being restricted to the
    // extension directory.
    FOLLOW_SYMLINKS_ANYWHERE = 1 << 5,

    // |ERROR_ON_PRIVATE_KEY| means that private keys inside an
    // extension should be errors rather than warnings.
    ERROR_ON_PRIVATE_KEY = 1 << 6,

    // |WAS_INSTALLED_BY_DEFAULT| installed by default when the profile was
    // created.
    WAS_INSTALLED_BY_DEFAULT = 1 << 7,

    // Unused - was part of an abandoned experiment.
    REQUIRE_PERMISSIONS_CONSENT = 1 << 8,

    // |IS_EPHEMERAL| identifies ephemeral apps (experimental), which are not
    // permanently installed.
    IS_EPHEMERAL = 1 << 9,

    // |WAS_INSTALLED_BY_OEM| installed by an OEM (e.g on Chrome OS) and should
    // be placed in a special OEM folder in the App Launcher. Note: OEM apps are
    // also installed by Default (i.e. WAS_INSTALLED_BY_DEFAULT is also true).
    WAS_INSTALLED_BY_OEM = 1 << 10,
  };

  static scoped_refptr<Extension> Create(const base::FilePath& path,
                                         Manifest::Location location,
                                         const base::DictionaryValue& value,
                                         int flags,
                                         std::string* error);

  // In a few special circumstances, we want to create an Extension and give it
  // an explicit id. Most consumers should just use the other Create() method.
  static scoped_refptr<Extension> Create(const base::FilePath& path,
                                         Manifest::Location location,
                                         const base::DictionaryValue& value,
                                         int flags,
                                         const ExtensionId& explicit_id,
                                         std::string* error);

  // Valid schemes for web extent URLPatterns.
  static const int kValidWebExtentSchemes;

  // Valid schemes for host permission URLPatterns.
  static const int kValidHostPermissionSchemes;

  // The mimetype used for extensions.
  static const char kMimeType[];

  // Checks to see if the extension has a valid ID.
  static bool IdIsValid(const std::string& id);

  // See Type definition in Manifest.
  Manifest::Type GetType() const;

  // Returns an absolute url to a resource inside of an extension. The
  // |extension_url| argument should be the url() from an Extension object. The
  // |relative_path| can be untrusted user input. The returned URL will either
  // be invalid() or a child of |extension_url|.
  // NOTE: Static so that it can be used from multiple threads.
  static GURL GetResourceURL(const GURL& extension_url,
                             const std::string& relative_path);
  GURL GetResourceURL(const std::string& relative_path) const {
    return GetResourceURL(url(), relative_path);
  }

  // Returns true if the resource matches a pattern in the pattern_set.
  bool ResourceMatches(const URLPatternSet& pattern_set,
                       const std::string& resource) const;

  // Returns an extension resource object. |relative_path| should be UTF8
  // encoded.
  ExtensionResource GetResource(const std::string& relative_path) const;

  // As above, but with |relative_path| following the file system's encoding.
  ExtensionResource GetResource(const base::FilePath& relative_path) const;

  // |input| is expected to be the text of an rsa public or private key. It
  // tolerates the presence or absence of bracking header/footer like this:
  //     -----(BEGIN|END) [RSA PUBLIC/PRIVATE] KEY-----
  // and may contain newlines.
  static bool ParsePEMKeyBytes(const std::string& input, std::string* output);

  // Does a simple base64 encoding of |input| into |output|.
  static bool ProducePEM(const std::string& input, std::string* output);

  // Expects base64 encoded |input| and formats into |output| including
  // the appropriate header & footer.
  static bool FormatPEMForFileOutput(const std::string& input,
                                     std::string* output,
                                     bool is_public);

  // Returns the base extension url for a given |extension_id|.
  static GURL GetBaseURLFromExtensionId(const ExtensionId& extension_id);

  // DEPRECATED: These methods have been moved to PermissionsData.
  // TODO(rdevlin.cronin): remove these once all calls have been updated.
  bool HasAPIPermission(APIPermission::ID permission) const;
  bool HasAPIPermission(const std::string& permission_name) const;
  scoped_refptr<const PermissionSet> GetActivePermissions() const;

  // Whether context menu should be shown for page and browser actions.
  bool ShowConfigureContextMenus() const;

  // Returns true if this extension or app includes areas within |origin|.
  bool OverlapsWithOrigin(const GURL& origin) const;

  // Returns true if the extension requires a valid ordinal for sorting, e.g.,
  // for displaying in a launcher or new tab page.
  bool RequiresSortOrdinal() const;

  // Returns true if the extension should be displayed in the app launcher.
  bool ShouldDisplayInAppLauncher() const;

  // Returns true if the extension should be displayed in the browser NTP.
  bool ShouldDisplayInNewTabPage() const;

  // Returns true if the extension should be displayed in the extension
  // settings page (i.e. chrome://extensions).
  bool ShouldDisplayInExtensionSettings() const;

  // Returns true if the extension should not be shown anywhere. This is
  // mostly the same as the extension being a component extension, but also
  // includes non-component apps that are hidden from the app launcher and ntp.
  bool ShouldNotBeVisible() const;

  // Get the manifest data associated with the key, or NULL if there is none.
  // Can only be called after InitValue is finished.
  ManifestData* GetManifestData(const std::string& key) const;

  // Sets |data| to be associated with the key. Takes ownership of |data|.
  // Can only be called before InitValue is finished. Not thread-safe;
  // all SetManifestData calls should be on only one thread.
  void SetManifestData(const std::string& key, ManifestData* data);

  // Accessors:

  const base::FilePath& path() const { return path_; }
  const GURL& url() const { return extension_url_; }
  Manifest::Location location() const;
  const ExtensionId& id() const;
  const base::Version* version() const { return version_.get(); }
  const std::string VersionString() const;
  const std::string& name() const { return name_; }
  const std::string& short_name() const { return short_name_; }
  const std::string& non_localized_name() const { return non_localized_name_; }
  // Base64-encoded version of the key used to sign this extension.
  // In pseudocode, returns
  // base::Base64Encode(RSAPrivateKey(pem_file).ExportPublicKey()).
  const std::string& public_key() const { return public_key_; }
  const std::string& description() const { return description_; }
  int manifest_version() const { return manifest_version_; }
  bool converted_from_user_script() const {
    return converted_from_user_script_;
  }
  PermissionsData* permissions_data() { return permissions_data_.get(); }
  const PermissionsData* permissions_data() const {
    return permissions_data_.get();
  }

  // Appends |new_warning[s]| to install_warnings_.
  void AddInstallWarning(const InstallWarning& new_warning);
  void AddInstallWarnings(const std::vector<InstallWarning>& new_warnings);
  const std::vector<InstallWarning>& install_warnings() const {
    return install_warnings_;
  }
  const extensions::Manifest* manifest() const {
    return manifest_.get();
  }
  bool wants_file_access() const { return wants_file_access_; }
  // TODO(rdevlin.cronin): This is needed for ContentScriptsHandler, and should
  // be moved out as part of crbug.com/159265. This should not be used anywhere
  // else.
  void set_wants_file_access(bool wants_file_access) {
    wants_file_access_ = wants_file_access;
  }
  int creation_flags() const { return creation_flags_; }
  bool from_webstore() const { return (creation_flags_ & FROM_WEBSTORE) != 0; }
  bool from_bookmark() const { return (creation_flags_ & FROM_BOOKMARK) != 0; }
  bool was_installed_by_default() const {
    return (creation_flags_ & WAS_INSTALLED_BY_DEFAULT) != 0;
  }
  bool was_installed_by_oem() const {
    return (creation_flags_ & WAS_INSTALLED_BY_OEM) != 0;
  }
  bool is_ephemeral() const { return (creation_flags_ & IS_EPHEMERAL) != 0; }

  // App-related.
  bool is_app() const;
  bool is_platform_app() const;
  bool is_hosted_app() const;
  bool is_legacy_packaged_app() const;
  bool is_extension() const;
  bool can_be_incognito_enabled() const;

  void AddWebExtentPattern(const URLPattern& pattern);
  const URLPatternSet& web_extent() const { return extent_; }

  // Theme-related.
  bool is_theme() const;

 private:
  friend class base::RefCountedThreadSafe<Extension>;

  // Chooses the extension ID for an extension based on a variety of criteria.
  // The chosen ID will be set in |manifest|.
  static bool InitExtensionID(extensions::Manifest* manifest,
                              const base::FilePath& path,
                              const ExtensionId& explicit_id,
                              int creation_flags,
                              base::string16* error);

  Extension(const base::FilePath& path,
            scoped_ptr<extensions::Manifest> manifest);
  virtual ~Extension();

  // Initialize the extension from a parsed manifest.
  // TODO(aa): Rename to just Init()? There's no Value here anymore.
  // TODO(aa): It is really weird the way this class essentially contains a copy
  // of the underlying DictionaryValue in its members. We should decide to
  // either wrap the DictionaryValue and go with that only, or we should parse
  // into strong types and discard the value. But doing both is bad.
  bool InitFromValue(int flags, base::string16* error);

  // The following are helpers for InitFromValue to load various features of the
  // extension from the manifest.

  bool LoadRequiredFeatures(base::string16* error);
  bool LoadName(base::string16* error);
  bool LoadVersion(base::string16* error);

  bool LoadAppFeatures(base::string16* error);
  bool LoadExtent(const char* key,
                  URLPatternSet* extent,
                  const char* list_error,
                  const char* value_error,
                  base::string16* error);

  bool LoadSharedFeatures(base::string16* error);
  bool LoadDescription(base::string16* error);
  bool LoadManifestVersion(base::string16* error);
  bool LoadShortName(base::string16* error);

  bool CheckMinimumChromeVersion(base::string16* error) const;

  // The extension's human-readable name. Name is used for display purpose. It
  // might be wrapped with unicode bidi control characters so that it is
  // displayed correctly in RTL context.
  // NOTE: Name is UTF-8 and may contain non-ascii characters.
  std::string name_;

  // A non-localized version of the extension's name. This is useful for
  // debug output.
  std::string non_localized_name_;

  // A short version of the extension's name. This can be used as an alternative
  // to the name where there is insufficient space to display the full name. If
  // an extension has not explicitly specified a short name, the value of this
  // member variable will be the full name rather than an empty string.
  std::string short_name_;

  // The version of this extension's manifest. We increase the manifest
  // version when making breaking changes to the extension system.
  // Version 1 was the first manifest version (implied by a lack of a
  // manifest_version attribute in the extension's manifest). We initialize
  // this member variable to 0 to distinguish the "uninitialized" case from
  // the case when we know the manifest version actually is 1.
  int manifest_version_;

  // The absolute path to the directory the extension is stored in.
  base::FilePath path_;

  // Defines the set of URLs in the extension's web content.
  URLPatternSet extent_;

  scoped_ptr<PermissionsData> permissions_data_;

  // Any warnings that occurred when trying to create/parse the extension.
  std::vector<InstallWarning> install_warnings_;

  // The base extension url for the extension.
  GURL extension_url_;

  // The extension's version.
  scoped_ptr<base::Version> version_;

  // An optional longer description of the extension.
  std::string description_;

  // True if the extension was generated from a user script. (We show slightly
  // different UI if so).
  bool converted_from_user_script_;

  // The public key used to sign the contents of the crx package.
  std::string public_key_;

  // The manifest from which this extension was created.
  scoped_ptr<Manifest> manifest_;

  // Stored parsed manifest data.
  ManifestDataMap manifest_data_;

  // Set to true at the end of InitValue when initialization is finished.
  bool finished_parsing_manifest_;

  // Ensures that any call to GetManifestData() prior to finishing
  // initialization happens from the same thread (this can happen when certain
  // parts of the initialization process need information from previous parts).
  base::ThreadChecker thread_checker_;

  // Should this app be shown in the app launcher.
  bool display_in_launcher_;

  // Should this app be shown in the browser New Tab Page.
  bool display_in_new_tab_page_;

  // Whether the extension has host permissions or user script patterns that
  // imply access to file:/// scheme URLs (the user may not have actually
  // granted it that access).
  bool wants_file_access_;

  // The flags that were passed to InitFromValue.
  int creation_flags_;

  DISALLOW_COPY_AND_ASSIGN(Extension);
};

typedef std::vector<scoped_refptr<const Extension> > ExtensionList;
typedef std::set<ExtensionId> ExtensionIdSet;
typedef std::vector<ExtensionId> ExtensionIdList;

// Handy struct to pass core extension info around.
struct ExtensionInfo {
  ExtensionInfo(const base::DictionaryValue* manifest,
                const ExtensionId& id,
                const base::FilePath& path,
                Manifest::Location location);
  ~ExtensionInfo();

  scoped_ptr<base::DictionaryValue> extension_manifest;
  ExtensionId extension_id;
  base::FilePath extension_path;
  Manifest::Location extension_location;

 private:
  DISALLOW_COPY_AND_ASSIGN(ExtensionInfo);
};

struct InstalledExtensionInfo {
  // The extension being installed - this should always be non-NULL.
  const Extension* extension;

  // True if the extension is being updated; false if it is being installed.
  bool is_update;

  // The name of the extension prior to this update. Will be empty if
  // |is_update| is false.
  std::string old_name;

  InstalledExtensionInfo(const Extension* extension,
                         bool is_update,
                         const std::string& old_name);
};

struct UnloadedExtensionInfo {
  // TODO(DHNishi): Move this enum to ExtensionRegistryObserver.
  enum Reason {
    REASON_UNDEFINED,         // Undefined state used to initialize variables.
    REASON_DISABLE,           // Extension is being disabled.
    REASON_UPDATE,            // Extension is being updated to a newer version.
    REASON_UNINSTALL,         // Extension is being uninstalled.
    REASON_TERMINATE,         // Extension has terminated.
    REASON_BLACKLIST,         // Extension has been blacklisted.
    REASON_PROFILE_SHUTDOWN,  // Profile is being shut down.
  };

  Reason reason;

  // The extension being unloaded - this should always be non-NULL.
  const Extension* extension;

  UnloadedExtensionInfo(const Extension* extension, Reason reason);
};

// The details sent for EXTENSION_PERMISSIONS_UPDATED notifications.
struct UpdatedExtensionPermissionsInfo {
  enum Reason {
    ADDED,    // The permissions were added to the extension.
    REMOVED,  // The permissions were removed from the extension.
  };

  Reason reason;

  // The extension who's permissions have changed.
  const Extension* extension;

  // The permissions that have changed. For Reason::ADDED, this would contain
  // only the permissions that have added, and for Reason::REMOVED, this would
  // only contain the removed permissions.
  const PermissionSet* permissions;

  UpdatedExtensionPermissionsInfo(
      const Extension* extension,
      const PermissionSet* permissions,
      Reason reason);
};

}  // namespace extensions

#endif  // EXTENSIONS_COMMON_EXTENSION_H_
