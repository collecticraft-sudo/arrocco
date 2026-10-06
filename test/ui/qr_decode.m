// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI - reads the QR codes in a screen image with the Mac's own detector (Core Image), so
// that the session scripts can check that a code on the panel really says what it should, the
// way a phone camera would read it. Nothing is downloaded and nothing leaves the Mac.
//
//   build: clang -fobjc-arc -framework Foundation -framework CoreImage qr_decode.m -o qr_decode
//   usage: qr_decode FRAME.png [...]
//          one line per file: the text of every code found in it, tab separated ("" for none)
#import <CoreImage/CoreImage.h>
#import <Foundation/Foundation.h>

int main(int argc, const char* argv[]) {
  @autoreleasepool {
    CIDetector* detector = [CIDetector detectorOfType:CIDetectorTypeQRCode
                                              context:nil
                                              options:@{CIDetectorAccuracy : CIDetectorAccuracyHigh}];
    int failed = 0;
    for (int i = 1; i < argc; ++i) {
      NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[i]]];
      CIImage* image = [CIImage imageWithContentsOfURL:url];
      if (image == nil) {
        fprintf(stderr, "qr_decode: cannot read %s\n", argv[i]);
        failed = 1;
        printf("\n");
        continue;
      }
      NSMutableArray<NSString*>* texts = [NSMutableArray array];
      for (CIFeature* feature in [detector featuresInImage:image]) {
        if ([feature isKindOfClass:[CIQRCodeFeature class]]) {
          NSString* text = ((CIQRCodeFeature*)feature).messageString;
          if (text != nil) [texts addObject:text];
        }
      }
      printf("%s\n", [[texts componentsJoinedByString:@"\t"] UTF8String]);
    }
    return failed;
  }
}
