# Data safety answers

Play Console > App content > Data safety. LeanDoc has no internet
permission and no third-party code that collects data, so every answer is
"no".

| Question | Answer |
| --- | --- |
| Does your app collect or share any of the required user data types? | No |
| Is all of the user data collected by your app encrypted in transit? | Not applicable: no data is collected or sent. |
| Do you provide a way for users to request that their data is deleted? | Not applicable: no data is collected. |

Why "no" is accurate:

- The manifest requests no permissions. Without `android.permission.INTERNET`
  the app cannot open network connections. CI checks the release APK for
  this on every pull request (`aapt2 dump permissions`).
- WebView's own usage metrics upload is turned off in the manifest
  (`android.webkit.WebView.MetricsOptOut`), and Safe Browsing lookups are
  off (`android.webkit.WebView.EnableSafeBrowsing`).
- No analytics, crash reporting, advertising or account libraries are
  included. The app has no AndroidX or other runtime libraries at all.
- The file a user opens is read in memory to show it and is not stored or
  sent anywhere. While Android keeps the screen's saved state, that state
  holds the open file's content:// address, so the file can be shown again
  after a restart of the app process. The app saves only the chosen theme
  and the chosen view (page or reading), on the device, and turns off
  Android backup (`android:allowBackup="false"`).
- Macros in .docm and .dotm files are never run, and images or templates a
  document links to on the internet are never fetched.

Data deletion: nothing to delete. Uninstalling removes the two saved
settings.
