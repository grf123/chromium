// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content.browser;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.Message;
import android.os.Messenger;
import android.os.RemoteException;
import android.support.test.InstrumentationRegistry;
import android.support.test.filters.MediumTest;

import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.BaseSwitches;
import org.chromium.base.ThreadUtils;
import org.chromium.base.library_loader.LibraryLoader;
import org.chromium.base.library_loader.LibraryProcessType;
import org.chromium.base.process_launcher.ChildConnectionAllocator;
import org.chromium.base.process_launcher.ChildProcessConnection;
import org.chromium.base.process_launcher.FileDescriptorInfo;
import org.chromium.base.test.util.DisabledTest;
import org.chromium.base.test.util.Feature;
import org.chromium.content.browser.test.ChildProcessAllocatorSettings;
import org.chromium.content.browser.test.ContentJUnit4ClassRunner;
import org.chromium.content.browser.test.util.Criteria;
import org.chromium.content.browser.test.util.CriteriaHelper;
import org.chromium.content_shell_apk.ChildProcessLauncherTestHelperService;
import org.chromium.content_shell_apk.ChildProcessLauncherTestUtils;

import java.util.concurrent.Callable;

/**
 * Instrumentation tests for ChildProcessLauncher.
 */
@RunWith(ContentJUnit4ClassRunner.class)
public class ChildProcessLauncherHelperTest {
    // Pseudo command line arguments to instruct the child process to wait until being killed.
    // Allowing the process to continue would lead to a crash when attempting to initialize IPC
    // channels that are not being set up in this test.
    private static final String[] sProcessWaitArguments = {
            "_", "--" + BaseSwitches.RENDERER_WAIT_FOR_JAVA_DEBUGGER};
    private static final String DEFAULT_SANDBOXED_PROCESS_SERVICE =
            "org.chromium.content.app.SandboxedProcessService";

    private static final int DONT_BLOCK = 0;
    private static final int BLOCK_UNTIL_CONNECTED = 1;
    private static final int BLOCK_UNTIL_SETUP = 2;

    @Before
    public void setUp() throws Exception {
        LibraryLoader.get(LibraryProcessType.PROCESS_CHILD).ensureInitialized();
    }

    /**
     * Tests binding to the same sandboxed service process from multiple processes in the
     * same package. This uses the ChildProcessLauncherTestHelperService declared in
     * ContentShell.apk as a separate android:process to bind the first (slot 0) service. The
     * instrumentation test then tries to bind the same slot, which fails, so the
     * ChildProcessLauncher retries on a new connection.
     */
    @Test
    //@MediumTest
    //@Feature({"ProcessManagement"})
    // Test is flaky: crbug.com/752691
    @DisabledTest
    @ChildProcessAllocatorSettings(
            sandboxedServiceCount = 2, sandboxedServiceName = DEFAULT_SANDBOXED_PROCESS_SERVICE)
    public void
    testBindServiceFromMultipleProcesses() throws RemoteException {
        final Context context = InstrumentationRegistry.getTargetContext();

        // Start the Helper service.
        class HelperConnection implements ServiceConnection {
            Messenger mMessenger = null;

            @Override
            public void onServiceConnected(ComponentName name, IBinder service) {
                mMessenger = new Messenger(service);
            }

            @Override
            public void onServiceDisconnected(ComponentName name) {}
        }
        final HelperConnection serviceConnection = new HelperConnection();

        Intent intent = new Intent();
        intent.setComponent(new ComponentName(context.getPackageName(),
                context.getPackageName() + ".ChildProcessLauncherTestHelperService"));
        Assert.assertTrue(context.bindService(intent, serviceConnection, Context.BIND_AUTO_CREATE));

        // Wait for the Helper service to connect.
        CriteriaHelper.pollInstrumentationThread(
                new Criteria("Failed to get helper service Messenger") {
                    @Override
                    public boolean isSatisfied() {
                        return serviceConnection.mMessenger != null;
                    }
                });

        Assert.assertNotNull(serviceConnection.mMessenger);

        class ReplyHandler implements Handler.Callback {
            Message mMessage;

            @Override
            public boolean handleMessage(Message msg) {
                // Copy the message so its contents outlive this Binder transaction.
                mMessage = Message.obtain();
                mMessage.copyFrom(msg);
                return true;
            }
        }
        final ReplyHandler replyHandler = new ReplyHandler();

        // Send a message to the Helper and wait for the reply. This will cause the slot 0
        // sandboxed service connection to be bound by a different PID (i.e., not this process).
        Message msg = Message.obtain(null, ChildProcessLauncherTestHelperService.MSG_BIND_SERVICE);
        msg.replyTo = new Messenger(new Handler(Looper.getMainLooper(), replyHandler));
        serviceConnection.mMessenger.send(msg);

        CriteriaHelper.pollInstrumentationThread(
                new Criteria("Failed waiting for helper service reply") {
                    @Override
                    public boolean isSatisfied() {
                        return replyHandler.mMessage != null;
                    }
                });

        // Verify that the Helper was able to launch the sandboxed service.
        Assert.assertNotNull(replyHandler.mMessage);
        Assert.assertEquals(ChildProcessLauncherTestHelperService.MSG_BIND_SERVICE_REPLY,
                replyHandler.mMessage.what);
        Assert.assertEquals(
                "Connection slot from helper service is not 0", 0, replyHandler.mMessage.arg2);

        final int helperConnectionPid = replyHandler.mMessage.arg1;
        Assert.assertTrue(helperConnectionPid > 0);

        // Launch a service from this process. Since slot 0 is already bound by the Helper, it
        // will fail to start and the ChildProcessLauncher will retry and use the slot 1.
        ChildProcessCreationParams.registerDefault(
                new ChildProcessCreationParams(context.getPackageName(),
                        false /* isExternalService */, LibraryProcessType.PROCESS_CHILD,
                        true /* bindToCallerCheck */, false /* ignoreVisibilityForImportance */));
        ChildProcessLauncherHelper launcher =
                startSandboxedChildProcess(BLOCK_UNTIL_SETUP, true /* doSetupConnection */);

        final ChildProcessConnection retryConnection =
                ChildProcessLauncherTestUtils.getConnection(launcher);
        Assert.assertEquals(
                1, ChildProcessLauncherTestUtils.getConnectionServiceNumber(retryConnection));

        ChildConnectionAllocator connectionAllocator =
                launcher.getChildConnectionAllocatorForTesting();

        // Check that only one connection is created.
        for (int i = 0; i < connectionAllocator.getNumberOfServices(); ++i) {
            ChildProcessConnection sandboxedConn =
                    connectionAllocator.getChildProcessConnectionAtSlotForTesting(i);
            if (i == 1) {
                Assert.assertNotNull(sandboxedConn);
                Assert.assertNotNull(
                        ChildProcessLauncherTestUtils.getConnectionService(sandboxedConn));
            } else {
                Assert.assertNull(sandboxedConn);
            }
        }

        Assert.assertEquals(
                connectionAllocator.getChildProcessConnectionAtSlotForTesting(1), retryConnection);

        CriteriaHelper.pollInstrumentationThread(
                new Criteria("Failed waiting retry connection to get pid") {
                    @Override
                    public boolean isSatisfied() {
                        return ChildProcessLauncherTestUtils.getConnectionPid(retryConnection) > 0;
                    }
                });
        Assert.assertTrue(ChildProcessLauncherTestUtils.getConnectionPid(retryConnection)
                != helperConnectionPid);
        Assert.assertTrue(
                ChildProcessLauncherTestUtils.getConnectionService(retryConnection).bindToCaller());

        // Unbind the service.
        replyHandler.mMessage = null;
        msg = Message.obtain(null, ChildProcessLauncherTestHelperService.MSG_UNBIND_SERVICE);
        msg.replyTo = new Messenger(new Handler(Looper.getMainLooper(), replyHandler));
        serviceConnection.mMessenger.send(msg);
        CriteriaHelper.pollInstrumentationThread(
                new Criteria("Failed waiting for helper service unbind reply") {
                    @Override
                    public boolean isSatisfied() {
                        return replyHandler.mMessage != null;
                    }
                });
        Assert.assertEquals(ChildProcessLauncherTestHelperService.MSG_UNBIND_SERVICE_REPLY,
                replyHandler.mMessage.what);

        // The 0th connection should now be usable.
        launcher = startSandboxedChildProcess(BLOCK_UNTIL_SETUP, true /* doSetupConnection */);
        ChildProcessConnection connection = ChildProcessLauncherTestUtils.getConnection(launcher);
        Assert.assertEquals(
                0, ChildProcessLauncherTestUtils.getConnectionServiceNumber(connection));
    }

    private static void warmUpOnUiThreadBlocking(final Context context) {
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                ChildProcessLauncherHelper.warmUp(context);
            }
        });
        ChildProcessConnection connection = getWarmUpConnection();
        Assert.assertNotNull(connection);
        blockUntilConnected(connection);
    }

    private void testWarmUpImpl() {
        Context context = InstrumentationRegistry.getTargetContext();
        warmUpOnUiThreadBlocking(context);

        Assert.assertEquals(1, getConnectedSandboxedServicesCount());

        ChildProcessLauncherHelper launcherHelper =
                startSandboxedChildProcess(BLOCK_UNTIL_SETUP, true /* doSetupConnection */);

        // The warm-up connection was used, so no new process should have been created.
        Assert.assertEquals(1, getConnectedSandboxedServicesCount());

        int pid = getPid(launcherHelper);
        Assert.assertNotEquals(0, pid);

        stopProcess(launcherHelper);

        waitForConnectedSandboxedServicesCount(0);
    }

    @Test
    @MediumTest
    @Feature({"ProcessManagement"})
    public void testWarmUp() {
        // Use the default creation parameters.
        testWarmUpImpl();
    }

    @Test
    @MediumTest
    @Feature({"ProcessManagement"})
    public void testWarmUpWithBindToCaller() {
        Context context = InstrumentationRegistry.getTargetContext();
        ChildProcessCreationParams.registerDefault(
                new ChildProcessCreationParams(context.getPackageName(),
                        false /* isExternalService */, LibraryProcessType.PROCESS_CHILD,
                        true /* bindToCallerCheck */, false /* ignoreVisibilityForImportance */));
        testWarmUpImpl();
    }

    // Tests that the warm-up connection is freed from its allocator if it crashes.
    @Test
    @MediumTest
    @Feature({"ProcessManagement"})
    public void testWarmUpProcessCrashBeforeUse() throws RemoteException {
        Assert.assertEquals(0, getConnectedSandboxedServicesCount());

        Context context = InstrumentationRegistry.getTargetContext();
        warmUpOnUiThreadBlocking(context);

        Assert.assertEquals(1, getConnectedSandboxedServicesCount());

        // Crash the warm-up connection before it gets used.
        ChildProcessConnection connection = getWarmUpConnection();
        Assert.assertNotNull(connection);
        connection.crashServiceForTesting();

        // It should get cleaned-up.
        waitForConnectedSandboxedServicesCount(0);

        // And subsequent process launches should work.
        ChildProcessLauncherHelper launcher =
                startSandboxedChildProcess(BLOCK_UNTIL_SETUP, true /* doSetupConnection */);
        Assert.assertEquals(1, getConnectedSandboxedServicesCount());
        Assert.assertNotNull(ChildProcessLauncherTestUtils.getConnection(launcher));
    }

    // Tests that the warm-up connection is freed from its allocator if it crashes after being used.
    @Test
    @MediumTest
    @Feature({"ProcessManagement"})
    public void testWarmUpProcessCrashAfterUse() throws RemoteException {
        Context context = InstrumentationRegistry.getTargetContext();
        warmUpOnUiThreadBlocking(context);

        Assert.assertEquals(1, getConnectedSandboxedServicesCount());

        ChildProcessLauncherHelper launcherHelper =
                startSandboxedChildProcess(BLOCK_UNTIL_SETUP, true /* doSetupConnection */);

        // The warm-up connection was used, so no new process should have been created.
        Assert.assertEquals(1, getConnectedSandboxedServicesCount());

        int pid = getPid(launcherHelper);
        Assert.assertNotEquals(0, pid);

        ChildProcessConnection connection = retrieveConnection(launcherHelper);
        connection.crashServiceForTesting();

        waitForConnectedSandboxedServicesCount(0);
    }

    @Test
    @MediumTest
    @Feature({"ProcessManagement"})
    public void testLauncherCleanup() throws RemoteException {
        ChildProcessLauncherHelper launcher =
                startSandboxedChildProcess(BLOCK_UNTIL_SETUP, true /* doSetupConnection */);
        int pid = getPid(launcher);
        Assert.assertNotEquals(0, pid);

        // Stop the process explicitly, the launcher should get cleared.
        stopProcess(launcher);
        waitForConnectedSandboxedServicesCount(0);

        launcher = startSandboxedChildProcess(BLOCK_UNTIL_SETUP, true /* doSetupConnection */);
        pid = getPid(launcher);
        Assert.assertNotEquals(0, pid);

        // This time crash the connection, the launcher should also get cleared.
        ChildProcessConnection connection = retrieveConnection(launcher);
        connection.crashServiceForTesting();
        waitForConnectedSandboxedServicesCount(0);
    }

    private static ChildProcessLauncherHelper startSandboxedChildProcess(
            int blockingPolicy, final boolean doSetupConnection) {
        assert doSetupConnection || blockingPolicy != BLOCK_UNTIL_SETUP;
        ChildProcessLauncherHelper launcher =
                ChildProcessLauncherTestUtils.runOnLauncherAndGetResult(
                        new Callable<ChildProcessLauncherHelper>() {
                            @Override
                            public ChildProcessLauncherHelper call() {
                                return ChildProcessLauncherHelper.createAndStartForTesting(
                                        sProcessWaitArguments, new FileDescriptorInfo[0],
                                        true /* sandboxed */, null /* binderCallback */,
                                        doSetupConnection);
                            }
                        });
        if (blockingPolicy != DONT_BLOCK) {
            assert blockingPolicy == BLOCK_UNTIL_CONNECTED || blockingPolicy == BLOCK_UNTIL_SETUP;
            blockUntilConnected(launcher);
            if (blockingPolicy == BLOCK_UNTIL_SETUP) {
                blockUntilSetup(launcher);
            }
        }
        return launcher;
    }

    private static void blockUntilConnected(final ChildProcessLauncherHelper launcher) {
        CriteriaHelper.pollInstrumentationThread(
                new Criteria("The connection wasn't established.") {
                    @Override
                    public boolean isSatisfied() {
                        return launcher.getChildProcessConnection() != null
                                && launcher.getChildProcessConnection().isConnected();
                    }
                });
    }

    private static void blockUntilConnected(final ChildProcessConnection connection) {
        CriteriaHelper.pollInstrumentationThread(
                new Criteria("The connection wasn't established.") {
                    @Override
                    public boolean isSatisfied() {
                        return connection.isConnected();
                    }
                });
    }

    private static void blockUntilSetup(final ChildProcessLauncherHelper launcher) {
        CriteriaHelper.pollInstrumentationThread(
                new Criteria("The connection wasn't established.") {
                    @Override
                    public boolean isSatisfied() {
                        return getPid(launcher) != 0;
                    }
                });
    }

    // Returns the number of sandboxed connection currently connected,
    private static int getConnectedSandboxedServicesCount() {
        return ChildProcessLauncherTestUtils.runOnLauncherAndGetResult(new Callable<Integer>() {
            @Override
            public Integer call() {
                return ChildProcessLauncherHelper.getConnectedSandboxedServicesCountForTesting();
            }
        });
    }

    // Blocks until the number of sandboxed connections reaches targetCount.
    private static void waitForConnectedSandboxedServicesCount(int targetCount) {
        CriteriaHelper.pollInstrumentationThread(
                Criteria.equals(targetCount, new Callable<Integer>() {
                    @Override
                    public Integer call() {
                        return getConnectedSandboxedServicesCount();
                    }
                }));
    }

    private static ChildProcessConnection retrieveConnection(
            final ChildProcessLauncherHelper launcherHelper) {
        CriteriaHelper.pollInstrumentationThread(
                new Criteria("Failed waiting for child process to connect") {
                    @Override
                    public boolean isSatisfied() {
                        return ChildProcessLauncherTestUtils.getConnection(launcherHelper) != null;
                    }
                });

        return ChildProcessLauncherTestUtils.getConnection(launcherHelper);
    }

    private static void stopProcess(ChildProcessLauncherHelper launcherHelper) {
        final ChildProcessConnection connection = retrieveConnection(launcherHelper);
        ChildProcessLauncherTestUtils.runOnLauncherThreadBlocking(new Runnable() {
            @Override
            public void run() {
                ChildProcessLauncherHelper.stop(connection.getPid());
            }
        });
    }

    private static void stopProcesses(ChildProcessLauncherHelper... launcherHelpers) {
        final int[] pids = new int[launcherHelpers.length];
        for (int i = 0; i < launcherHelpers.length; i++) {
            pids[i] = getPid(launcherHelpers[i]);
        }
        ChildProcessLauncherTestUtils.runOnLauncherThreadBlocking(new Runnable() {
            @Override
            public void run() {
                for (int pid : pids) {
                    ChildProcessLauncherHelper.stop(pid);
                }
            }
        });
    }

    private static int getPid(final ChildProcessLauncherHelper launcherHelper) {
        return ChildProcessLauncherTestUtils.runOnLauncherAndGetResult(new Callable<Integer>() {
            @Override
            public Integer call() {
                return launcherHelper.getPid();
            }
        });
    }

    private static ChildProcessConnection getWarmUpConnection() {
        return ChildProcessLauncherTestUtils.runOnLauncherAndGetResult(
                new Callable<ChildProcessConnection>() {
                    @Override
                    public ChildProcessConnection call() {
                        return ChildProcessLauncherHelper.getWarmUpConnectionForTesting();
                    }
                });
    }
}
