; This tests the optimization of atomic cmpxchg w/ following cmp + branches.

; RUN: %p2i -i %s --args -O2 --verbose none \
; RUN:   | llvm-mc -triple=i686-none-nacl -x86-asm-syntax=intel -filetype=obj \
; RUN:   | llvm-objdump -d -symbolize -x86-asm-syntax=intel - \
; RUN:   | FileCheck --check-prefix=O2 %s
; RUN: %p2i -i %s --args -Om1 --verbose none \
; RUN:   | llvm-mc -triple=i686-none-nacl -x86-asm-syntax=intel -filetype=obj \
; RUN:   | llvm-objdump -d -symbolize -x86-asm-syntax=intel - \
; RUN:   | FileCheck --check-prefix=OM1 %s
; RUN: %p2i -i %s --args --verbose none | FileCheck --check-prefix=ERRORS %s
; TODO(kschimpf) Find out why lc2i is needed.
; RUN: %lc2i -i %s --insts | %szdiff %s | FileCheck --check-prefix=DUMP %s

declare i32 @llvm.nacl.atomic.cmpxchg.i32(i32*, i32, i32, i32, i32)


; Test that a cmpxchg followed by icmp eq and branch can be optimized to
; reuse the flags set by the cmpxchg instruction itself.
; This is only expected to work w/ O2, based on lightweight liveness.
; (Or if we had other means to detect the only use).
declare void @use_value(i32)

define i32 @test_atomic_cmpxchg_loop(i32 %iptr, i32 %expected, i32 %desired) {
entry:
  br label %loop

loop:
  %expected_loop = phi i32 [ %expected, %entry ], [ %old, %loop ]
  %succeeded_first_try = phi i32 [ 1, %entry ], [ 2, %loop ]
  %ptr = inttoptr i32 %iptr to i32*
  %old = call i32 @llvm.nacl.atomic.cmpxchg.i32(i32* %ptr, i32 %expected_loop,
                                                i32 %desired, i32 6, i32 6)
  %success = icmp eq i32 %expected_loop, %old
  br i1 %success, label %done, label %loop

done:
  call void @use_value(i32 %old)
  ret i32 %succeeded_first_try
}
; O2-LABEL: test_atomic_cmpxchg_loop
; O2: lock
; O2-NEXT: cmpxchg dword ptr [e{{[^a].}}], e{{[^a]}}
; O2-NOT: cmp
; Make sure the phi assignment for succeeded_first_try is still there.
; O2: mov {{.*}}, 2
; O2-NOT: cmp
; O2: jne
; Make sure the call isn't accidentally deleted.
; O2: call
;
; Check that the unopt version does have a cmp
; OM1-LABEL: test_atomic_cmpxchg_loop
; OM1: lock
; OM1-NEXT: cmpxchg dword ptr [e{{[^a].}}], e{{[^a]}}
; OM1: cmp
; OM1: je
; OM1: call

; Still works if the compare operands are flipped.
define i32 @test_atomic_cmpxchg_loop2(i32 %iptr, i32 %expected, i32 %desired) {
entry:
  br label %loop

loop:
  %expected_loop = phi i32 [ %expected, %entry ], [ %old, %loop ]
  %ptr = inttoptr i32 %iptr to i32*
  %old = call i32 @llvm.nacl.atomic.cmpxchg.i32(i32* %ptr, i32 %expected_loop,
                                                i32 %desired, i32 6, i32 6)
  %success = icmp eq i32 %old, %expected_loop
  br i1 %success, label %done, label %loop

done:
  ret i32 %old
}
; O2-LABEL: test_atomic_cmpxchg_loop2
; O2: lock
; O2-NEXT: cmpxchg dword ptr [e{{[^a].}}], e{{[^a]}}
; O2-NOT: cmp
; O2: jne


; Still works if the compare operands are constants.
define i32 @test_atomic_cmpxchg_loop_const(i32 %iptr, i32 %desired) {
entry:
  br label %loop

loop:
  %succeeded_first_try = phi i32 [ 1, %entry ], [ 0, %loop ]
  %ptr = inttoptr i32 %iptr to i32*
  %old = call i32 @llvm.nacl.atomic.cmpxchg.i32(i32* %ptr, i32 0,
                                                i32 %desired, i32 6, i32 6)
  %success = icmp eq i32 %old, 0
  br i1 %success, label %done, label %loop

done:
  ret i32 %succeeded_first_try
}
; O2-LABEL: test_atomic_cmpxchg_loop_const
; O2: lock
; O2-NEXT: cmpxchg dword ptr [e{{[^a].}}], e{{[^a]}}
; O2-NOT: cmp
; O2: jne

; This is a case where the flags cannot be reused (compare is for some
; other condition).
define i32 @test_atomic_cmpxchg_no_opt(i32 %iptr, i32 %expected, i32 %desired) {
entry:
  br label %loop

loop:
  %expected_loop = phi i32 [ %expected, %entry ], [ %old, %loop ]
  %ptr = inttoptr i32 %iptr to i32*
  %old = call i32 @llvm.nacl.atomic.cmpxchg.i32(i32* %ptr, i32 %expected_loop,
                                                i32 %desired, i32 6, i32 6)
  %success = icmp sgt i32 %old, %expected
  br i1 %success, label %done, label %loop

done:
  ret i32 %old
}
; O2-LABEL: test_atomic_cmpxchg_no_opt
; O2: lock
; O2-NEXT: cmpxchg dword ptr [e{{[^a].}}], e{{[^a]}}
; O2: mov {{.*}}
; O2: cmp
; O2: jle

; Another case where the flags cannot be reused (the comparison result
; is used somewhere else).
define i32 @test_atomic_cmpxchg_no_opt2(i32 %iptr, i32 %expected, i32 %desired) {
entry:
  br label %loop

loop:
  %expected_loop = phi i32 [ %expected, %entry ], [ %old, %loop ]
  %ptr = inttoptr i32 %iptr to i32*
  %old = call i32 @llvm.nacl.atomic.cmpxchg.i32(i32* %ptr, i32 %expected_loop,
                                                i32 %desired, i32 6, i32 6)
  %success = icmp eq i32 %old, %expected
  br i1 %success, label %done, label %loop

done:
  %r = zext i1 %success to i32
  ret i32 %r
}
; O2-LABEL: test_atomic_cmpxchg_no_opt2
; O2: lock
; O2-NEXT: cmpxchg dword ptr [e{{[^a].}}], e{{[^a]}}
; O2: mov {{.*}}
; O2: cmp
; O2: je

; ERRORS-NOT: ICE translation error
; DUMP-NOT: SZ
