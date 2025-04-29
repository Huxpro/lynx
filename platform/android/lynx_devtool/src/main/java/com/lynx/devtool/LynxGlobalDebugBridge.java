// Copyright 2020 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

package com.lynx.devtool;

import android.content.Context;
import android.util.Log;
import android.view.ViewGroup;
import androidx.annotation.Keep;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import com.lynx.debugrouter.ConnectionState;
import com.lynx.debugrouter.ConnectionType;
import com.lynx.debugrouter.DebugRouter;
import com.lynx.debugrouter.DebugRouterGlobalHandler;
import com.lynx.debugrouter.StateListener;
import com.lynx.devtoolwrapper.LynxDevtoolCardListener;
import com.lynx.tasm.LynxEnv;
import com.lynx.tasm.LynxEnvKey;
import com.lynx.tasm.base.LLog;
import com.lynx.tasm.eventreport.ILynxEventReportObserver;
import com.lynx.tasm.eventreport.LynxEventReporter;
import java.lang.ref.WeakReference;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import org.json.JSONException;
import org.json.JSONObject;

@Keep
public class LynxGlobalDebugBridge
    implements DebugRouterGlobalHandler, StateListener, ILynxEventReportObserver {
  private static final String TAG = "LynxGlobalDebugBridge";

  // protocol
  private static final String GET_STOP_AT_ENTRY = "GetStopAtEntry";
  private static final String SET_STOP_AT_ENTRY = "SetStopAtEntry";
  private static final String CUSTOM_FOR_SET_GLOBAL_SWITCH = "SetGlobalSwitch";
  private static final String CUSTOM_FOR_GET_GLOBAL_SWITCH = "GetGlobalSwitch";
  private static final String KEY_TYPE = "type";
  private static final String KEY_VALUE = "value";
  private static final String KEY_MTS = "MTS";
  private static final String KEY_BTS = "BTS";
  private static final String KEY_DEFAULT = "DEFAULT";

  private boolean mHasContext = false;
  private Context mContext;

  // debug status view
  private WeakReference<ViewGroup> mRootView = null;

  private DevToolAgentDispatcher mAgentDispatcher;

  private Set<LynxDevtoolCardListener> mCardListeners = new HashSet<>();

  // Singleton
  public static LynxGlobalDebugBridge getInstance() {
    return SingletonHolder.INSTANCE;
  }

  private static class SingletonHolder {
    private static final LynxGlobalDebugBridge INSTANCE = new LynxGlobalDebugBridge();
  }

  private static class DevToolAgentDispatcher extends LynxInspectorOwner {}

  private LynxGlobalDebugBridge() {
    mAgentDispatcher = new DevToolAgentDispatcher();
    DebugRouter.getInstance().addGlobalHandler(this);
    DebugRouter.getInstance().addStateListener(this);
    LynxEventReporter.addObserver(this);
  }

  public void setContext(Context ctx) {
    if (mHasContext) {
      return;
    }
    mContext = ctx;
    mHasContext = true;
  }

  public boolean shouldPrepareRemoteDebug(String url) {
    return DebugRouter.getInstance().isValidSchema(url);
  }

  public boolean prepareRemoteDebug(String scheme) {
    return DebugRouter.getInstance().handleSchema(scheme);
  }

  public void setAppInfo(Context context, Map<String, String> appInfo) {
    if (appInfo == null) {
      return;
    }
    appInfo.put("sdkVersion", LynxEnv.inst().getLynxVersion());
    DebugRouter.getInstance().setAppInfo(context, appInfo);
  }

  public boolean isEnabled() {
    return DebugRouter.getInstance().getConnectionState() == ConnectionState.CONNECTED;
  }

  public void registerCardListener(LynxDevtoolCardListener listener) {
    if (listener != null) {
      mCardListeners.add(listener);
    }
  }

  @Override
  public void openCard(String url) {
    for (LynxDevtoolCardListener listener : mCardListeners) {
      LLog.i(TAG, "openCard: " + url + ", handled by " + listener.getClass().getName());
      listener.open(url);
    }
  }

  @Override
  public void onMessage(String type, int sessionId, String message) {
    if (type == null || message == null) {
      return;
    }
    if (type.equals(CUSTOM_FOR_SET_GLOBAL_SWITCH)) {
      Object result = mAgentDispatcher.setGlobalSwitch(message);
      DebugRouter.getInstance().sendDataAsync(
          CUSTOM_FOR_SET_GLOBAL_SWITCH, -1, String.valueOf(result));
    } else if (type.equals(CUSTOM_FOR_GET_GLOBAL_SWITCH)) {
      Object result = mAgentDispatcher.getGlobalSwitch(message);
      DebugRouter.getInstance().sendDataAsync(
          CUSTOM_FOR_GET_GLOBAL_SWITCH, -1, String.valueOf(result));
    } else {
      handleStopAtEntry(message, type);
    }
  }

  private void handleStopAtEntry(String message, String type) {
    if (!type.equals(GET_STOP_AT_ENTRY) && !type.equals(SET_STOP_AT_ENTRY)) {
      return;
    }
    try {
      JSONObject messageObj = new JSONObject(message);
      String key = messageObj.getString(KEY_TYPE);
      if (type.equals(GET_STOP_AT_ENTRY)) {
        boolean result = false;
        if (key.equals(KEY_MTS)) {
          result = mAgentDispatcher.getStopAtEntry(true);
        } else if (key.equals(KEY_BTS) || key.equals(KEY_DEFAULT)) {
          result = mAgentDispatcher.getStopAtEntry(false);
        }
        messageObj.put(KEY_VALUE, result);
      } else if (type.equals(SET_STOP_AT_ENTRY)) {
        boolean value = messageObj.getBoolean(KEY_VALUE);
        if (key.equals(KEY_MTS)) {
          mAgentDispatcher.setStopAtEntry(value, true);
        } else if (key.equals(KEY_BTS) || key.equals(KEY_DEFAULT)) {
          mAgentDispatcher.setStopAtEntry(value, false);
        }
      }
      DebugRouter.getInstance().sendDataAsync(type, -1, messageObj.toString());
    } catch (JSONException e) {
      LLog.e(TAG,
          String.format("handleStopAtEntry error! message: %s, type: %s, description: %s", message,
              type, e.getMessage()));
    }
  }

  public void startRecord() {
    RecorderController.nativeStartRecord();
  }

  @Override
  public void onClose(int code, String reason) {
    enableTraceMode(false);
  }

  private void enableTraceMode(boolean enable) {
    LynxDevtoolEnv.inst().setDevtoolEnvMask(LynxEnvKey.SP_KEY_ENABLE_DOM_TREE, !enable);
    LynxDevtoolEnv.inst().setDevtoolEnvMask(LynxEnvKey.SP_KEY_ENABLE_QUICKJS_DEBUG, !enable);
    LynxDevtoolEnv.inst().setDevtoolEnvMask(LynxEnvKey.SP_KEY_ENABLE_V8, !enable);
    LynxDevtoolEnv.inst().setDevtoolEnvMask(LynxEnvKey.SP_KEY_ENABLE_PREVIEW_SCREEN_SHOT, !enable);
    LynxDevtoolEnv.inst().setDevtoolEnvMask(LynxEnvKey.SP_KEY_ENABLE_HIGHLIGHT_TOUCH, !enable);
  }

  @Override
  public void onError(String error) {
    enableTraceMode(false);
  }

  @Override
  public void onOpen(ConnectionType type) {
    LynxDevtoolEnv.inst().setDevtoolEnv(LynxEnvKey.SP_KEY_DEVTOOL_CONNECTED, true);
  }

  @Override
  public void onMessage(String text) {}

  @Override
  public void onReportEvent(@NonNull String eventName, int instanceId,
      @NonNull Map<String, ?> props, @Nullable Map<String, ?> extraData) {
    onPerfMetricsEvent(eventName, new JSONObject(props), instanceId);
  }

  public void onPerfMetricsEvent(String eventName, @NonNull JSONObject data, int instanceId) {
    if (LynxDevtoolEnv.inst().isPerfMetricsEnabled() && mAgentDispatcher != null) {
      try {
        data.put("instanceId", instanceId);
        mAgentDispatcher.onPerfMetricsEvent(eventName, data);
      } catch (JSONException e) {
        LLog.e(TAG, e.toString());
      }
    }
  }
}
