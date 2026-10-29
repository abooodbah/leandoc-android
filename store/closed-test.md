# Closed test plan

Google Play requires new personal developer accounts to run a closed test
with at least 12 testers who stay opted in for 14 days in a row before the
app can be published to production. This is the plan for LeanDoc.

## Before the test

1. Build and sign the bundle with the upload key (see "Signing for Google
   Play" in the README).
2. In Play Console, create the app, fill in the store listing from the
   files in this folder, and answer the data safety and content rating
   questions with `data-safety.md` and `content-rating.md`.
3. Testing > Closed testing: create a track, upload the bundle, and add the
   testers' Google account addresses as an email list (or a Google Group).
4. Send the testers the opt-in link from the track page. Each tester has to
   open it, accept, and install the app from Play.

## Testers

- At least 12 people, plus a few spare. Play counts testers who have been
  opted in for the last 14 days without a break; if that number drops
  below 12, the 14 days start over. Inviting 15 or more leaves room.
- Each needs an Android phone with Android 8 or later and Google Play.
- Ask for a mix of Android versions and phone makers.

## What to ask testers to do

Over the 14 days, at least once:

- Open a .docx from the Files app, from a mail attachment and from the
  browser's downloads, and use the Open button in the app.
- Open a long document (50 pages or more) and scroll through it.
- Switch between the page view and the reading view, use Find, switch
  between light and dark, rotate the phone.
- Open a .docm file and check that the app says it has macros and does
  not run them.

Ask them to report anything that does not open, looks wrong, or is slow,
with the Android version and phone model. The app collects nothing, so
reports have to come from testers directly (mail, or Play's private
feedback on the testing page).

## During the test

- Keep the build installed; ship fixes as new closed-test releases. A new
  release does not restart the 14 days.
- Check the Play Console pre-launch report for crashes on Google's test
  devices.

## After 14 days

Play Console > Dashboard shows when the requirement is met. Then apply for
production access, answer the questions about the test, and promote the
tested release.
