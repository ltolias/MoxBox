

import _ from 'underscore'
import React from 'react'

const StreamToggle = (props) => {

    return (
        <label className='flex-row input-container' style={{position: 'absolute', top: '10px', right: '10px', height: '20px'}}>
            <span className='input-label light' style={{fontSize: '.7em', width: '40px'}}>Stream</span> 
            <div  className='input-divider'/>
            <div className='input-label dark checkbox-container'>
                <div className='checkbox-box'>
                    <input type='checkbox' onClick={props.onToggle}/>
                </div>
            </div>
        </label>
    )
}

export default StreamToggle
