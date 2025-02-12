

import _ from 'underscore'
import React from 'react'
import { useState, useEffect, useRef} from 'react'

import Utilities from './utilities'

import Camera from './Camera'
import StreamToggle from './StreamToggle'
import Datum from './Datum'

const Dashboard = (props) => {
    
    const [state, setState] = useState({
        sensor_data: { 
            target: 1, 
            background: 2 
        }
    });

    const util = Utilities(state, setState, props.server_ip);
    var data_streaming_ref = useRef(false);
    
    useEffect(() => {
        (async () => {
            try {
                data_streaming_ref.period = 1000
                await util.device_to_state('sensor_data')
            } catch (error) {
                console.error('Error fetching initial data:', error);
            }
        })();
        return () => {
            data_streaming_ref.current = false;
        }
    }, []);

    const data = _.map(state.sensor_data, (val, key) => 
        <Datum label={key} key={key} value={val}/>
    )
    
    const data_streaming_render = <StreamToggle 
        onToggle={util.toggle_looper(() => util.device_to_state('sensor_data'), data_streaming_ref)} 
    />
    
    return [
        <div className='pane top-pane' key='1'>
            <div className='flex-row wrap-container justify-left' style={{position: 'relative'}}>
                <h4> Sensors </h4>
                {data_streaming_render}
            </div>
            <div style={{height: '12px'}}/>     
            <div className='flex-row wrap-container' >
                {data} 
            </div>
            <div style={{height: '15px'}}/>      
        </div>,
        <div className='pane bottom-pane' key='2'>
            <Camera server_ip={state.server_ip}/>  
        </div>
    ]
    
}

export default Dashboard



