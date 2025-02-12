import React from 'react';
import { useState, useEffect, useRef, useCallback } from 'react'
import _ from 'underscore';

import ConfigInput from './ConfigInput'
import StreamToggle from './StreamToggle'
import Utilities from './utilities'
import localforage from 'localforage';

const Settings = (props) => {

    const fetch_timeout_ms_stored = useCallback(
        () => localStorage.getItem('fetch_timeout_ms') ? 
            localStorage.getItem('fetch_timeout_ms') : 60000, 
    []) //makes this only load from localstorage once
    
    const [state, setState] = useState({
        config: { 
            outlier_setting: 200, 
            xclk_freq_mhz: 5, 
            jpeg_quality: 6 
        },
        mcuinfo: '',
        fetch_timeout_ms: fetch_timeout_ms_stored()
    })
    const util = Utilities(state, setState, props.server_ip);

    var ping_streaming_ref = useRef(false);
    var mcuinfo_streaming_ref = useRef(false);
   
    useEffect(() => {
        (async () => {
            try {
                ping_streaming_ref.period = 1000
                mcuinfo_streaming_ref.period = 5000
                await util.device_to_state('config')
                await util.device_to_state('mcuinfo')
            } catch (error) {
                console.error('Error fetching data: ', error);
            }
        })();
        return () => {
            ping_streaming_ref.current = false;
            mcuinfo_streaming_ref.current = false;
        }
    }, []);
    
    const reload_app = () => location.reload()
    
    const reset_app = () => {
        localStorage.clear();
        localforage.clear();
    }
    
    const update_app = async () => {
        const registrations = await navigator.serviceWorker.getRegistrations();
        const unregisterPromises = registrations.map(registration => registration.unregister());

        const allCaches = await caches.keys();
        const cacheDeletionPromises = allCaches.map(cache => caches.delete(cache));

        await Promise.all([...unregisterPromises, ...cacheDeletionPromises]);
        await reload_app()
    }

    const change_timeout = (e) => {
        navigator.serviceWorker.controller.postMessage({ fetch_timeout_ms: e.target.value });
        localStorage.setItem('fetch_timeout_ms', e.target.value)
        setState((prevState) => ({...prevState, fetch_timeout_ms: e.target.value}))
    }

    const config_inputs = _.map(state.config, (val, key) => 
        <ConfigInput key={key} param={key} value={val} onChange={util.config_changer(key)}/>
    )

    const mcuinfo_streaming_render = <StreamToggle 
        onToggle={util.toggle_looper(() => util.device_to_state('mcuinfo'), mcuinfo_streaming_ref)} 
    />

    return [
        <div className='pane top-pane' key='1'>
            <div className='flex-row wrap-container justify-left'>
                <h4> App </h4>
            </div>
            <div style={{height: '10px'}}/>
            <div className='flex-row wrap-container justify-left'>
                <button className='input-container dark' onClick={() => util.fetch('restart')}>
                    Restart Device
                </button>
                <button className='input-container dark' onClick={reload_app}>
                    Reload Page
                </button>
                <button className='input-container dark' onClick={update_app}>
                    Update App
                </button>
                <button className='input-container dark' onClick={reset_app}>
                    Reset App
                </button>
                <ConfigInput param='Timeout (ms)'  value={state.fetch_timeout_ms} onChange={change_timeout}/>
                <label className='flex-row input-container'>
                    <span className='input-label label-narrow light'>Ping</span> 
                    <div  className='input-divider'/>
                    <div className='input-label dark checkbox-container'>
                        <div className='checkbox-box'>
                            <input type='checkbox' onClick={util.toggle_looper(() => util.fetch('ping'), ping_streaming_ref)}/>
                        </div>
                    </div>
                </label>
            </div>
            <div style={{height: '10px'}}/>      
        </div>,
        <div className='pane' key='2'>
            
            <div className='flex-row wrap-container justify-left'>
                <h4> Device </h4>
            </div>
            <div style={{height: '10px'}}/>
            <div className='flex-row wrap-container justify-left'>
                {config_inputs}
            </div>
            <div style={{height: '15px'}}/>
            <div className='flex-row wrap-container justify-right'>
                <button className='input-container dark' onClick={() => util.device_to_state('config')}>
                    Load
                </button>
                <button className='input-container dark' onClick={() => util.state_to_device('config', state.config)}>
                    Save
                </button>
            </div>
            <div style={{height: '10px'}}/>
        </div>,
        <div className='pane bottom-pane' key='3'>
            <div className='flex-row wrap-container justify-left' style={{position: 'relative'}}>
                <h4> Activity Monitor </h4>
                {mcuinfo_streaming_render}
            </div>
            <div style={{height: '20px'}}/>
            <pre className='mcu-info' >{state.mcuinfo}</pre>
            <div style={{height: '10px'}}/>
        </div>
    ]
  
};

export default Settings;