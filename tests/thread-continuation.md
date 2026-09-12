# Thread continuation regression checks

Run against a newly built Scylla with a signed-in Codex runtime.

1. Send a message, close Scylla, reopen the saved chat, and immediately send a
   follow-up with an image attachment. Verify the response retains the previous
   context and receives the image. The outgoing RPC sequence must acknowledge
   `thread/resume` before sending `turn/start`; the user message appears once.
2. Restart the runtime by changing the project grant, then continue the same
   chat. Verify the thread resumes before the turn starts.
3. Switch from chat A to chat B while A is resuming. Verify the late response
   never changes the selected chat and the queued turn still targets A.
4. Cancel while a resume is pending. Verify the eventual resume response does
   not submit the cancelled turn.
5. Use an unavailable thread ID in a disposable test chat. Verify a resume
   error finishes the activity, preserves the message, and sends no turn/start.

These checks require a live runtime; they have not been run by the source edit.
