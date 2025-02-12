import React from 'react';
import { useState, useEffect, useRef } from 'react'


import ConfigInput from './ConfigInput'
import StreamToggle from './StreamToggle'

import Utilities from './utilities'


const Camera = (props) => {

    const [state, setState] = useState({
        config: {
            frame_period_ms: 1000
        },
        image_data: null,
    })
    const util = Utilities(state, setState, props.server_ip);

    var streaming_enabled_ref = useRef(false);

    useEffect(() => {
        (async () => {
            try {
                streaming_enabled_ref.period = state.config.frame_period_ms
            } catch (error) {  }
        })();
        return () => {
            streaming_enabled_ref.current = false;
        }
    }, [state.config.frame_period_ms]);
    

    const get_frame = async () => {
        const response = await util.fetch('frame');
        if (response.status == 200) {
            const imageBlob =  await response.blob();

            const imageObjectURL = URL.createObjectURL(imageBlob);

            if (state.image_data) {
                URL.revokeObjectURL(state.image_data);
            }
            await setState((prevState) => ({...prevState, image_data: imageObjectURL}))
        }
    };
    
    const toggle_lamp = async (event) => {
        let value = 0
        if (event.target.checked) {
          value = 1
        }
        util.fetch('lamp?value=' + encodeURIComponent(value))
    }
    
    const frame = state.image_data ? <img src={state.image_data} className='frame'/> : 
        <img src="madeline.jpg" className='frame cat' style={{width: '100%', transform: 'rotate(0deg)'}}/>

    const camera_streaming_render = <StreamToggle 
        onToggle={util.toggle_looper(get_frame, streaming_enabled_ref)} 
    />


    return [
    <div className='flex-row wrap-container justify-left' style={{position: 'relative'}} key='1'>
        <h4> Camera </h4>
        {camera_streaming_render}
    </div>,
    <div style={{height: '12px'}} key='2'/>,
    <div className='flex-row wrap-container' key='3'>
        <div className='frame-container' > 
            {frame}
        </div>
        <div className='flex-row wrap-container'>
            <button className='input-container dark' onClick={() => util.fetch('startcam')}> Start Camera </button>
            <label className='flex-row input-container'>
                <span className='input-label label-narrow light'> Lamp </span> 
                <div className='input-label dark checkbox-container'>
                    <div className='checkbox-box'>
                        <input type='checkbox' onClick={toggle_lamp}/>
                    </div>
                </div>
            </label>
            <ConfigInput 
                key="frame_time" param="Frame period (ms):" 
                value={state.config.frame_period_ms} 
                onChange={util.config_changer('frame_period_ms')}
            />
        </div>
        <div style={{height: '10px'}}/>
    </div>,
    <div style={{height: '15px'}} key='4'/>]
};

export default Camera;