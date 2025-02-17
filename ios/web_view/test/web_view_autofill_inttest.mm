// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <ChromeWebView/ChromeWebView.h>
#import <Foundation/Foundation.h>

#include "base/strings/stringprintf.h"
#include "base/strings/sys_string_conversions.h"
#import "ios/testing/wait_util.h"
#import "ios/web_view/test/web_view_int_test.h"
#import "ios/web_view/test/web_view_test_util.h"
#import "net/base/mac/url_conversions.h"
#include "testing/gtest_mac.h"
#import "third_party/ocmock/OCMock/OCMock.h"
#include "url/gurl.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

using testing::kWaitForActionTimeout;
using testing::WaitUntilConditionOrTimeout;

namespace ios_web_view {

namespace {
NSString* const kTestFormName = @"FormName";
NSString* const kTestFormID = @"FormID";
NSString* const kTestFieldName = @"FieldName";
NSString* const kTestFieldID = @"FieldID";
NSString* const kTestFieldValue = @"FieldValue";
NSString* const kTestSubmitID = @"SubmitID";
NSString* const kTestFormHtml =
    [NSString stringWithFormat:
                  @"<form name='%@' id='%@'>"
                   "<input type='text' name='%@' id='%@'/>"
                   "<input type='submit' id='%@'/>"
                   "</form>",
                  kTestFormName,
                  kTestFormID,
                  kTestFieldName,
                  kTestFieldID,
                  kTestSubmitID];
}  // namespace

// Tests autofill features in CWVWebViews.
class WebViewAutofillTest : public WebViewIntTest {
 protected:
  WebViewAutofillTest() : autofill_controller_(web_view_.autofillController) {}

  bool LoadTestPage() WARN_UNUSED_RESULT {
    std::string html = base::SysNSStringToUTF8(kTestFormHtml);
    GURL url = GetUrlForPageWithHtmlBody(html);
    return test::LoadUrl(web_view_, net::NSURLWithGURL(url));
  }

  bool SubmitForm() WARN_UNUSED_RESULT {
    NSString* submit_script =
        [NSString stringWithFormat:@"document.getElementById('%@').click();",
                                   kTestSubmitID];
    NSError* submit_error = nil;
    test::EvaluateJavaScript(web_view_, submit_script, &submit_error);
    return !submit_error;
  }

  bool SetFormFieldValue(NSString* value) WARN_UNUSED_RESULT {
    NSString* set_value_script = [NSString
        stringWithFormat:@"document.getElementById('%@').value = '%@';",
                         kTestFieldID, value];
    NSError* set_value_error = nil;
    test::EvaluateJavaScript(web_view_, set_value_script, &set_value_error);
    return !set_value_error;
  }

  NSArray<CWVAutofillSuggestion*>* FetchSuggestions() {
    __block bool suggestions_fetched = false;
    __block NSArray<CWVAutofillSuggestion*>* fetched_suggestions = nil;
    [autofill_controller_
        fetchSuggestionsForFormWithName:kTestFormName
                              fieldName:kTestFieldName
                      completionHandler:^(
                          NSArray<CWVAutofillSuggestion*>* suggestions) {
                        fetched_suggestions = suggestions;
                        suggestions_fetched = true;
                      }];
    EXPECT_TRUE(WaitUntilConditionOrTimeout(kWaitForActionTimeout, ^bool {
      return suggestions_fetched;
    }));
    return fetched_suggestions;
  }

  CWVAutofillController* autofill_controller_;
};

// Tests that CWVAutofillControllerDelegate receives callbacks.
TEST_F(WebViewAutofillTest, TestDelegateCallbacks) {
  ASSERT_TRUE(LoadTestPage());
  ASSERT_TRUE(SetFormFieldValue(kTestFieldValue));

  id delegate = OCMProtocolMock(@protocol(CWVAutofillControllerDelegate));
  autofill_controller_.delegate = delegate;

  [[delegate expect] autofillController:autofill_controller_
                didFocusOnFieldWithName:kTestFieldName
                               formName:kTestFormName
                                  value:kTestFieldValue];
  NSString* focus_script =
      [NSString stringWithFormat:
                    @"var event = new Event('focus');"
                     "document.getElementById('%@').dispatchEvent(event);",
                    kTestFieldID];
  NSError* focus_error = nil;
  test::EvaluateJavaScript(web_view_, focus_script, &focus_error);
  ASSERT_NSEQ(nil, focus_error);
  [delegate verify];

  [[delegate expect] autofillController:autofill_controller_
                 didBlurOnFieldWithName:kTestFieldName
                               formName:kTestFormName
                                  value:kTestFieldValue];
  NSString* blur_script =
      [NSString stringWithFormat:
                    @"var event = new Event('blur');"
                     "document.getElementById('%@').dispatchEvent(event);",
                    kTestFieldID];
  NSError* blur_error = nil;
  test::EvaluateJavaScript(web_view_, blur_script, &blur_error);
  ASSERT_NSEQ(nil, blur_error);
  [delegate verify];

  [[delegate expect] autofillController:autofill_controller_
                didInputInFieldWithName:kTestFieldName
                               formName:kTestFormName
                                  value:kTestFieldValue];
  // The 'input' event listener defined in form.js is only called during the
  // bubbling phase.
  NSString* input_script =
      [NSString stringWithFormat:
                    @"var event = new Event('input', {'bubbles': true});"
                     "document.getElementById('%@').dispatchEvent(event);",
                    kTestFieldID];
  NSError* input_error = nil;
  test::EvaluateJavaScript(web_view_, input_script, &input_error);
  ASSERT_NSEQ(nil, input_error);
  [delegate verify];

  [[delegate expect] autofillController:autofill_controller_
                  didSubmitFormWithName:kTestFormName
                          userInitiated:NO
                            isMainFrame:YES];
  // The 'submit' event listener defined in form.js is only called during the
  // bubbling phase.
  NSString* submit_script =
      [NSString stringWithFormat:
                    @"var event = new Event('submit', {'bubbles': true});"
                     "document.getElementById('%@').dispatchEvent(event);",
                    kTestFormID];
  NSError* submit_error = nil;
  test::EvaluateJavaScript(web_view_, submit_script, &submit_error);
  ASSERT_NSEQ(nil, submit_error);
  [delegate verify];
}

// Tests that CWVAutofillController can fetch, fill, and clear suggestions.
TEST_F(WebViewAutofillTest, TestSuggestionFetchFillClear) {
  ASSERT_TRUE(LoadTestPage());
  ASSERT_TRUE(SetFormFieldValue(kTestFieldValue));
  ASSERT_TRUE(SubmitForm());
  ASSERT_TRUE(LoadTestPage());

  NSArray<CWVAutofillSuggestion*>* fetched_suggestions = FetchSuggestions();
  EXPECT_EQ(1U, fetched_suggestions.count);
  CWVAutofillSuggestion* fetched_suggestion = fetched_suggestions.firstObject;
  EXPECT_NSEQ(kTestFieldValue, fetched_suggestion.value);
  EXPECT_NSEQ(kTestFormName, fetched_suggestion.formName);
  EXPECT_NSEQ(kTestFieldName, fetched_suggestion.fieldName);

  // The input element needs to be focused before it can be filled or cleared.
  NSString* focus_script = [NSString
      stringWithFormat:@"document.getElementById('%@').focus()", kTestFieldID];
  NSError* focus_error = nil;
  test::EvaluateJavaScript(web_view_, focus_script, &focus_error);
  ASSERT_NSEQ(nil, focus_error);

  __block bool suggestion_filled = false;
  [autofill_controller_ fillSuggestion:fetched_suggestion
                     completionHandler:^{
                       suggestion_filled = true;
                     }];
  EXPECT_TRUE(WaitUntilConditionOrTimeout(kWaitForActionTimeout, ^bool {
    return suggestion_filled;
  }));
  NSString* filled_script = [NSString
      stringWithFormat:@"document.getElementById('%@').value", kTestFieldID];
  NSError* filled_error = nil;
  NSString* filled_value =
      test::EvaluateJavaScript(web_view_, filled_script, &filled_error);
  ASSERT_NSEQ(nil, filled_error);
  EXPECT_NSEQ(fetched_suggestion.value, filled_value);

  __block bool form_cleared = false;
  [autofill_controller_ clearFormWithName:kTestFormName
                        completionHandler:^{
                          form_cleared = true;
                        }];
  EXPECT_TRUE(WaitUntilConditionOrTimeout(kWaitForActionTimeout, ^bool {
    return form_cleared;
  }));
  NSString* cleared_script = [NSString
      stringWithFormat:@"document.getElementById('%@').value", kTestFieldID];
  NSError* cleared_error = nil;
  NSString* current_value =
      test::EvaluateJavaScript(web_view_, cleared_script, &cleared_error);
  ASSERT_NSEQ(nil, cleared_error);
  EXPECT_NSEQ(@"", current_value);
}

// Tests that CWVAutofillController can remove a suggestion.
TEST_F(WebViewAutofillTest, TestSuggestionFetchRemoveFetch) {
  ASSERT_TRUE(LoadTestPage());
  ASSERT_TRUE(SetFormFieldValue(kTestFieldValue));
  ASSERT_TRUE(SubmitForm());
  ASSERT_TRUE(LoadTestPage());

  NSArray* fetched_suggestions_after_creating = FetchSuggestions();
  EXPECT_EQ(1U, fetched_suggestions_after_creating.count);

  CWVAutofillSuggestion* suggestion_to_remove =
      fetched_suggestions_after_creating.firstObject;
  [autofill_controller_ removeSuggestion:suggestion_to_remove];

  NSArray* fetched_suggestions_after_removing = FetchSuggestions();
  EXPECT_EQ(0U, fetched_suggestions_after_removing.count);
}

}  // namespace ios_web_view
