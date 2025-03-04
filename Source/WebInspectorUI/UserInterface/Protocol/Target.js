/*
 * Copyright (C) 2016 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

WebInspector.Target = class Target extends WebInspector.Object
{
    constructor(identifier, name, type, connection)
    {
        super();

        this._identifier = identifier;
        this._name = name;
        this._type = type;
        this._connection = connection;
        this._executionContext = null;
        this._mainResource = null;
        this._resourceCollection = new WebInspector.ResourceCollection;
        this._extraScriptCollection = new WebInspector.Collection(WebInspector.Collection.TypeVerifier.Script);

        this._connection.target = this;

        this.initialize();
    }

    // Agents

    get RuntimeAgent() { return this._connection._agents.Runtime; }
    get ConsoleAgent() { return this._connection._agents.Console; }
    get DebuggerAgent() { return this._connection._agents.Debugger; }

    // Public

    get identifier() { return this._identifier; }
    get name() { return this._name; }
    get type() { return this._type; }
    get connection() { return this._connection; }
    get executionContext() { return this._executionContext; }

    get resourceCollection() { return this._resourceCollection; }
    get extraScriptCollection() { return this._extraScriptCollection; }

    get mainResource() { return this._mainResource; }
    set mainResource(resource) { this._mainResource = resource; }

    addResource(resource)
    {
        this._resourceCollection.add(resource);

        this.dispatchEventToListeners(WebInspector.Target.Event.ResourceAdded, {resource});
    }

    adoptResource(resource)
    {
        resource._target = this;

        this.addResource(resource);
    }

    addScript(script)
    {
        this._extraScriptCollection.add(script);

        this.dispatchEventToListeners(WebInspector.Target.Event.ScriptAdded, {script});
    }
};

WebInspector.Target.Type = {
    Main: Symbol("main"),
    Worker: Symbol("worker"),
};

WebInspector.Target.Event = {
    ResourceAdded: "target-resource-added",
    ScriptAdded: "target-script-added",
};

WebInspector.MainTarget = class MainTarget extends WebInspector.Target
{
    constructor(connection)
    {
        super("", "", WebInspector.Target.Type.Main, InspectorBackend.mainConnection);
    }

    // Protected (Target)

    get displayName()
    {
        if (WebInspector.debuggableType === WebInspector.DebuggableType.Web)
            return WebInspector.UIString("Main Frame");
        return WebInspector.UIString("Main Context");
    }

    get mainResource()
    {
        let mainFrame = WebInspector.frameResourceManager.mainFrame;
        return mainFrame ? mainFrame.mainResource : null;
    }

    initialize()
    {
        this._executionContext = new WebInspector.ExecutionContext(this, WebInspector.RuntimeManager.TopLevelContextExecutionIdentifier, this.displayName, true, null);
    }
}

WebInspector.WorkerTarget = class WorkerTarget extends WebInspector.Target
{
    constructor(workerId, name, connection)
    {
        super(workerId, name, WebInspector.Target.Type.Worker, connection);
    }

    // Protected (Target)

    get displayName()
    {
        return WebInspector.displayNameForURL(this._name);
    }

    initialize()
    {
        WebInspector.frameResourceManager.adoptOrphanedResourcesForTarget(this);

        if (this.RuntimeAgent) {
            this._executionContext = new WebInspector.ExecutionContext(this, WebInspector.RuntimeManager.TopLevelContextExecutionIdentifier, this.displayName, false, null);
            this.RuntimeAgent.enable();
            if (WebInspector.showJavaScriptTypeInformationSetting && WebInspector.showJavaScriptTypeInformationSetting.value)
                this.RuntimeAgent.enableTypeProfiler();
            if (WebInspector.enableControlFlowProfilerSetting && WebInspector.enableControlFlowProfilerSetting.value)
                this.RuntimeAgent.enableControlFlowProfiler();
        }

        if (this.DebuggerAgent)
            WebInspector.debuggerManager.initializeTarget(this);

        if (this.ConsoleAgent)
            this.ConsoleAgent.enable();
    }
}
